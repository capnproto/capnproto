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
#include "message.h"
#include "descriptor.h"
#include <limits>

namespace capnproto {
namespace internal {

namespace debug {

bool fieldIsStruct(const StructDescriptor* descriptor, uint fieldNumber, uint refIndex) {
  return descriptor->fieldCount > fieldNumber &&
         descriptor->fields[fieldNumber].size == FieldSize::REFERENCE &&
         descriptor->fields[fieldNumber].offset == refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::STRUCT;
}

bool fieldIsList(const StructDescriptor* descriptor, uint fieldNumber, uint refIndex) {
  return descriptor->fieldCount > fieldNumber &&
         descriptor->fields[fieldNumber].size == FieldSize::REFERENCE &&
         descriptor->fields[fieldNumber].offset == refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::LIST;
}

bool fieldIsData(const StructDescriptor* descriptor, uint fieldNumber, uint dataOffset,
                 BitCount bitSize) {
  return descriptor->fieldCount > fieldNumber &&
         descriptor->fields[fieldNumber].size != FieldSize::REFERENCE &&
         sizeInBits(descriptor->fields[fieldNumber].size) == bitSize &&
         descriptor->fields[fieldNumber].offset == dataOffset;
}

bool dataFieldInRange(const StructDescriptor* descriptor, uint dataOffset, ByteCount size) {
  return descriptor->dataSize * BYTES_PER_WORD >= dataOffset * size;
}

bool bitFieldInRange(const StructDescriptor* descriptor, BitCount offset) {
  return descriptor->dataSize * BITS_PER_WORD >= offset;
}

bool refFieldIsStruct(const StructDescriptor* descriptor, uint refIndex) {
  return descriptor->referenceCount > refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::STRUCT;
}

bool refFieldIsList(const StructDescriptor* descriptor, uint refIndex) {
  return descriptor->referenceCount > refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::LIST;
}

bool elementsAreStructs(const ListDescriptor* descriptor) {
  return descriptor->elementSize == FieldSize::STRUCT;
}
bool elementsAreStructs(const ListDescriptor* descriptor, WordCount wordSize) {
  return descriptor->elementSize == FieldSize::STRUCT &&
         descriptor->elementDescriptor->asStruct()->wordSize() == wordSize;
}
bool elementsAreLists(const ListDescriptor* descriptor) {
  return descriptor->elementSize == FieldSize::REFERENCE;
}
bool elementsAreData(const ListDescriptor* descriptor, BitCount bitSize) {
  switch (descriptor->elementSize) {
    case FieldSize::REFERENCE:
    case FieldSize::KEY_REFERENCE:
    case FieldSize::STRUCT:
      return false;
    default:
      return sizeInBits(descriptor->elementSize) == bitSize;
  }
}

}  // namespace debug

// =======================================================================================

static const WordCount WORDS_PER_REFERENCE = 1 * WORDS;

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
    FAR = 3,
    RESERVED_4 = 4,
    RESERVED_5 = 5,
    RESERVED_6 = 6,
    RESERVED_7 = 7
  };

  WireValue<uint32_t> offsetAndTag;

  union {
    struct {
      WireValue<uint8_t> fieldCount;
      WireValue<WordCount8> dataWordCount;
      WireValue<uint8_t> refCount;
      WireValue<uint8_t> reserved0;

      inline WordCount wordSize() const {
        return dataWordCount.get() + refCount.get() * WORDS_PER_REFERENCE;
      }
    } structRef;
    // Also covers capabilities.

    struct {
      WireValue<uint32_t> elementSizeAndCount;

      CAPNPROTO_ALWAYS_INLINE(FieldSize elementSize() const) {
        return static_cast<FieldSize>(elementSizeAndCount.get() >> 29);
      }
      CAPNPROTO_ALWAYS_INLINE(uint elementCount() const) {
        return elementSizeAndCount.get() & 0x1fffffffu;
      }
    } listRef;

    struct {
      WireValue<SegmentId> segmentId;
    } farRef;
  };

  CAPNPROTO_ALWAYS_INLINE(bool isNull() const) { return offsetAndTag.get() == 0; }
  CAPNPROTO_ALWAYS_INLINE(WordCount offset() const) {
    return (offsetAndTag.get() >> 3) * WORDS;
  }
  CAPNPROTO_ALWAYS_INLINE(Tag tag() const) {
    return static_cast<Tag>(offsetAndTag.get() & 7);
  }

  CAPNPROTO_ALWAYS_INLINE(void setTagAndOffset(Tag tag, WordCount offset)) {
    offsetAndTag.set(((offset / WORDS) << 3) | tag);
  }

  CAPNPROTO_ALWAYS_INLINE(void setStruct(const StructDescriptor* descriptor, WordCount offset)) {
    setTagAndOffset(STRUCT, offset);
    structRef.fieldCount.set(descriptor->fieldCount);
    structRef.dataWordCount.set(WordCount8(descriptor->dataSize));
    structRef.refCount.set(descriptor->referenceCount);
    structRef.reserved0.set(0);
  }

  CAPNPROTO_ALWAYS_INLINE(void setList(
      const ListDescriptor* descriptor, int elementCount, WordCount offset)) {
    setTagAndOffset(LIST, offset);
    CAPNPROTO_DEBUG_ASSERT((elementCount >> 29) == 0, "Lists are limited to 2**29 elements.");
    listRef.elementSizeAndCount.set(
        (static_cast<int>(descriptor->elementSize) << 29) | elementCount);
  }

  CAPNPROTO_ALWAYS_INLINE(void setFar(SegmentId segmentId, WordCount offset)) {
    setTagAndOffset(FAR, offset);
    farRef.segmentId.set(segmentId);
  }
};
static_assert(sizeof(WireReference) == sizeof(word),
    "Layout of capnproto::WireReference is wrong.  It must be exactly one word or all hell will "
    "break loose.  (At the very least WORDS_PER_REFERENCE needs updating, but that's "
    "probably not all.)");

// =======================================================================================

template <typename T, typename U>
static inline decltype(T() / U()) divRoundingUp(T a, U b) {
  return (a + b - 1) / b;
}

template <typename T, typename U>
static CAPNPROTO_ALWAYS_INLINE(T divRoundingUp(UnitMeasure<T, U> a, UnitMeasure<T, U> b));
template <typename T, typename U>
static inline T divRoundingUp(UnitMeasure<T, U> a, UnitMeasure<T, U> b) {
  return (a + b - UnitMeasure<T, U>::ONE) / b;
}

template <typename T, typename T2, typename U, typename U2>
static CAPNPROTO_ALWAYS_INLINE(
    decltype(UnitMeasure<T, U>() / UnitRatio<T2, U, U2>(1))
    divRoundingUp(UnitMeasure<T, U> a, UnitRatio<T2, U, U2> b));
template <typename T, typename T2, typename U, typename U2>
static inline decltype(UnitMeasure<T, U>() / UnitRatio<T2, U, U2>(1))
divRoundingUp(UnitMeasure<T, U> a, UnitRatio<T2, U, U2> b) {
  return (a + (UnitMeasure<T2, U2>::ONE * b - UnitMeasure<T2, U>::ONE)) / b;
}

struct WireHelpers {
  static CAPNPROTO_ALWAYS_INLINE(word* allocate(
      WireReference*& ref, SegmentBuilder*& segment, WordCount amount)) {
    word* ptr = segment->allocate(amount);

    if (ptr == nullptr) {
      // Need to allocate in a new segment.

      // Loop here just in case we ever make Segment::allocate() thread-safe -- in this case another
      // thread could have grabbed the space between when we asked the message for the segment and
      // when we asked the segment to allocate space.
      do {
        segment = segment->getMessage()->getSegmentWithAvailable(amount + WORDS_PER_REFERENCE);
        ptr = segment->allocate(amount + WORDS_PER_REFERENCE);
      } while (CAPNPROTO_EXPECT_FALSE(ptr == nullptr));

      ref->setFar(segment->getSegmentId(), segment->getOffsetTo(ptr));
      ref = reinterpret_cast<WireReference*>(ptr);

      // Allocated space follows new reference.
      return ptr + WORDS_PER_REFERENCE;
    } else {
      return ptr;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(void followFars(WireReference*& ref, SegmentBuilder*& segment)) {
    if (ref->tag() == WireReference::FAR) {
      segment = segment->getMessage()->getSegment(ref->farRef.segmentId.get());
      ref = reinterpret_cast<WireReference*>(segment->getPtrUnchecked(ref->offset()));
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(bool followFars(
      const WireReference*& ref, SegmentReader*& segment)) {
    if (ref->tag() == WireReference::FAR) {
      segment = segment->getMessage()->tryGetSegment(ref->farRef.segmentId.get());
      if (CAPNPROTO_EXPECT_FALSE(segment == nullptr)) {
        return false;
      }
      ref = reinterpret_cast<const WireReference*>(
          segment->getPtrChecked(ref->offset(), 0 * WORDS, WORDS_PER_REFERENCE));
      return ref != nullptr;
    } else {
      return true;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(bool isStructCompatible(
      const StructDescriptor* descriptor, const WireReference* ref)) {
    if (ref->structRef.fieldCount.get() >= descriptor->fieldCount) {
      // The incoming struct has all of the fields that we know about.
      return ref->structRef.dataWordCount.get() >= descriptor->dataSize &&
             ref->structRef.refCount.get() >= descriptor->referenceCount;
    } else if (ref->structRef.fieldCount.get() > 0) {
      // We know about more fields than the struct has, and the struct is non-empty.
      const FieldDescriptor* field = &descriptor->fields[ref->structRef.fieldCount.get() - 1];
      return ref->structRef.dataWordCount.get() >= field->requiredDataSize &&
             ref->structRef.refCount.get() >= field->requiredReferenceCount;
    } else {
      // The incoming struct has no fields, so is necessarily compatible.
      return true;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(StructBuilder initStructReference(
      const StructDescriptor* descriptor, WireReference* ref, SegmentBuilder* segment)) {
    if (ref->isNull()) {
      // Allocate space for the new struct.
      word* ptr = allocate(ref, segment, descriptor->wordSize());

      // Advance the pointer to point between the data and reference segments.
      ptr += descriptor->dataSize;

      // Initialize the reference.
      ref->setStruct(descriptor, segment->getOffsetTo(ptr));

      // Build the StructBuilder.
      return StructBuilder(descriptor, segment, ptr);
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

      return StructBuilder(descriptor, segment, segment->getPtrUnchecked(ref->offset()));
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder initListReference(
      const ListDescriptor* descriptor, WireReference* ref,
      SegmentBuilder* segment, uint elementCount)) {
    if (descriptor->elementSize == FieldSize::STRUCT) {
      const StructDescriptor* elementDescriptor =
          descriptor->elementDescriptor->asStruct();

      // Allocate the list, prefixed by a single WireReference.
      word* ptr = allocate(ref, segment,
          WORDS_PER_REFERENCE + elementDescriptor->wordSize() * elementCount);

      // Initialize the reference.
      ref->setList(descriptor, elementCount, segment->getOffsetTo(ptr));

      // The list is prefixed by a struct reference.
      WireReference* structRef = reinterpret_cast<WireReference*>(ptr);
      word* structPtr = ptr + WORDS_PER_REFERENCE + elementDescriptor->dataSize;
      structRef->setStruct(elementDescriptor, segment->getOffsetTo(structPtr));

      // Build the ListBuilder.
      return ListBuilder(descriptor, segment, ptr, elementCount);
    } else {
      // Calculate size of the list.  Need to cast to uint here because a list can be up to
      // 2**32-1 bits, so int would overflow.  Plus uint division by a power of 2 is a bit shift.
      WordCount wordCount = divRoundingUp(
          sizeInBits(descriptor->elementSize) * elementCount, BITS_PER_WORD);

      // Allocate the list.
      word* ptr = allocate(ref, segment, wordCount);

      // Initialize the reference.
      ref->setList(descriptor, elementCount, segment->getOffsetTo(ptr));

      // Build the ListBuilder.
      return ListBuilder(descriptor, segment, ptr, elementCount);
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder getWritableListReference(
      const ListDescriptor* descriptor, WireReference* ref, SegmentBuilder* segment)) {
    if (ref->isNull()) {
      return ListBuilder(descriptor, segment, nullptr, 0);
    }

    followFars(ref, segment);

    CAPNPROTO_ASSERT(ref->tag() == WireReference::LIST,
        "Called getList{Field,Element}() but existing reference is not a list.");

    if (descriptor->elementSize == FieldSize::STRUCT) {
      WireReference* structRef = reinterpret_cast<WireReference*>(
          segment->getPtrUnchecked(ref->offset()));
      return ListBuilder(descriptor, segment,
          segment->getPtrUnchecked(structRef->offset()), ref->listRef.elementCount());
    } else {
      return ListBuilder(descriptor, segment,
          segment->getPtrUnchecked(ref->offset()), ref->listRef.elementCount());
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(StructReader readStructReference(
      const StructDescriptor* descriptor, const WireReference* ref,
      SegmentReader* segment, int recursionLimit)) {
    do {
      if (ref == nullptr || ref->isNull()) {
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
        segment->getMessage()->reportInvalidData(
            "Message is too deeply-nested or contains cycles.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(!followFars(ref, segment))) {
        segment->getMessage()->reportInvalidData(
            "Message contains out-of-bounds far reference.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::STRUCT)) {
        segment->getMessage()->reportInvalidData(
            "Message contains non-struct reference where struct reference was expected.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(!isStructCompatible(descriptor, ref))) {
        segment->getMessage()->reportInvalidData(
            "Message contains struct that is too small for its field count.");
        break;
      }

      const word* ptr = segment->getPtrChecked(ref->offset(),
          ref->structRef.dataWordCount.get(),
          ref->structRef.refCount.get() * WORDS_PER_REFERENCE);

      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        segment->getMessage()->reportInvalidData(
            "Message contained out-of-bounds struct reference.");
        break;
      }

      return StructReader(descriptor, segment, ptr, descriptor->defaultData,
                          ref->structRef.fieldCount.get(), 0 * BITS, recursionLimit - 1);
    } while (false);

    return StructReader(descriptor, segment, nullptr, descriptor->defaultData, 0, 0 * BITS, 0);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListReader readListReference(
      const ListDescriptor* descriptor, const WireReference* ref,
      SegmentReader* segment, int recursionLimit)) {
    do {
      if (ref == nullptr || ref->isNull()) {
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
        segment->getMessage()->reportInvalidData(
            "Message is too deeply-nested or contains cycles.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(!followFars(ref, segment))) {
        segment->getMessage()->reportInvalidData(
            "Message contains out-of-bounds far reference.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::LIST)) {
        segment->getMessage()->reportInvalidData(
            "Message contains non-list reference where list reference was expected.");
        break;
      }

      if (ref->listRef.elementSize() == FieldSize::STRUCT) {
        // A struct list reference actually points to a struct reference which in turn points to the
        // first struct in the list.
        const word* ptrPtr =
            segment->getPtrChecked(ref->offset(), 0 * WORDS, WORDS_PER_REFERENCE);
        if (CAPNPROTO_EXPECT_FALSE(ptrPtr == nullptr)) {
          segment->getMessage()->reportInvalidData(
              "Message contains out-of-bounds list reference.");
          break;
        }

        int size = ref->listRef.elementCount();
        ref = reinterpret_cast<const WireReference*>(ptrPtr);

        if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::STRUCT)) {
          segment->getMessage()->reportInvalidData(
              "Message contains struct list reference that does not point to a struct reference.");
          break;
        }

        const void* ptr = segment->getPtrChecked(ref->offset(),
            ref->structRef.dataWordCount.get(),
            ref->structRef.refCount.get() * WORDS_PER_REFERENCE +
            ref->structRef.wordSize() * (size - 1));
        if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
          segment->getMessage()->reportInvalidData(
              "Message contains out-of-bounds struct list reference.");
          break;
        }

        // If a struct list was not expected, then presumably a non-struct list was upgraded to a
        // struct list.  We need to manipulate the pointer to point at the first field of the
        // struct.  Together with the "stepBits", this will allow the struct list to be accessed as
        // if it were a primitive list without branching.
        ptr = reinterpret_cast<const byte*>(ptr) - byteOffsetForFieldZero(descriptor->elementSize);

        // Check whether the size is compatible.
        bool compatible = true;
        switch (descriptor->elementSize) {
          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES:
            compatible = ref->structRef.dataWordCount.get() > 0 * WORDS;
            break;

          case FieldSize::REFERENCE:
            compatible = ref->structRef.refCount.get() > 0;
            break;

          case FieldSize::KEY_REFERENCE:
            compatible = ref->structRef.dataWordCount.get() > 0 * WORDS &&
                         ref->structRef.refCount.get() > 0;
            break;

          case FieldSize::STRUCT: {
            compatible = isStructCompatible(descriptor->elementDescriptor->asStruct(), ref);
            break;
          }
        }

        if (CAPNPROTO_EXPECT_FALSE(!compatible)) {
          segment->getMessage()->reportInvalidData("A list had incompatible element type.");
          break;
        }

        return ListReader(descriptor, segment, ptr, size, ref->structRef.wordSize() * BITS_PER_WORD,
            ref->structRef.fieldCount.get(), recursionLimit - 1);

      } else {
        // The elements of the list are NOT structs.
        BitCount step = sizeInBits(ref->listRef.elementSize());

        const void* ptr = segment->getPtrChecked(ref->offset(), 0 * WORDS,
            divRoundingUp(ref->listRef.elementCount() * BitCount64(step), BITS_PER_WORD));

        if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
          segment->getMessage()->reportInvalidData(
              "Message contained out-of-bounds list reference.");
          break;
        }

        if (descriptor->elementSize == ref->listRef.elementSize()) {
          return ListReader(descriptor, segment, ptr, ref->listRef.elementCount(),
                            sizeInBits(ref->listRef.elementSize()), 0, recursionLimit);
        } else if (descriptor->elementSize == FieldSize::STRUCT) {
          // We were expecting a struct, but we received a list of some other type.  Perhaps a
          // non-struct list was recently upgraded to a struct list, but the sender is using the
          // old version of the protocol.  We need to verify that the struct's first field matches
          // what the sender sent us.
          const StructDescriptor* elementDescriptor = descriptor->elementDescriptor->asStruct();
          if (CAPNPROTO_EXPECT_FALSE(
              elementDescriptor->fieldCount == 0 ||
              elementDescriptor->fields[0].size != ref->listRef.elementSize())) {
            segment->getMessage()->reportInvalidData("A list had incompatible element type.");
            break;
          }

          // Adjust the pointer to point where we expect it for a struct.
          ptr = reinterpret_cast<const byte*>(ptr) +
              byteOffsetForFieldZero(descriptor->elementSize);

          return ListReader(descriptor, segment, ptr, ref->listRef.elementCount(),
                            sizeInBits(ref->listRef.elementSize()), 1, recursionLimit);
        } else {
          segment->getMessage()->reportInvalidData("A list had incompatible element type.");
          break;
        }
      }
    } while (false);

    switch (descriptor->elementSize) {
      case FieldSize::REFERENCE:
      case FieldSize::KEY_REFERENCE:
      case FieldSize::STRUCT:
        return ListReader(descriptor, segment, nullptr, descriptor->defaultCount, 0 * BITS, 0,
            recursionLimit - 1);
      default:
        return ListReader(descriptor, segment, descriptor->defaultData, descriptor->defaultCount,
            sizeInBits(descriptor->elementSize), 0, recursionLimit - 1);
    }
  }
};

// =======================================================================================

StructBuilder StructBuilder::getStructFieldInternal(int refIndex) const {
  return WireHelpers::initStructReference(
      descriptor->defaultReferences[refIndex]->asStruct(),
      reinterpret_cast<WireReference*>(ptr) + refIndex, segment);
}

ListBuilder StructBuilder::initListFieldInternal(int refIndex, uint32_t elementCount) const {
  return WireHelpers::initListReference(
      descriptor->defaultReferences[refIndex]->asList(),
      reinterpret_cast<WireReference*>(ptr) + refIndex, segment, elementCount);
}

ListBuilder StructBuilder::getListFieldInternal(int refIndex) const {
  return WireHelpers::getWritableListReference(
      descriptor->defaultReferences[refIndex]->asList(),
      reinterpret_cast<WireReference*>(ptr) + refIndex, segment);
}

StructReader StructBuilder::asReader() const {
  return StructReader(descriptor, segment, ptr, descriptor->defaultData,
                      descriptor->fieldCount, 0 * BITS, std::numeric_limits<int>::max());
}

StructReader StructReader::getStructFieldInternal(int fieldNumber, unsigned int refIndex) const {
  return WireHelpers::readStructReference(
      descriptor->defaultReferences[refIndex]->asStruct(),
      fieldNumber < fieldCount
          ? reinterpret_cast<const WireReference*>(ptr) + refIndex
          : nullptr,
      segment, recursionLimit);
}

ListReader StructReader::getListFieldInternal(int fieldNumber, unsigned int refIndex) const {
  return WireHelpers::readListReference(
      descriptor->defaultReferences[refIndex]->asList(),
      fieldNumber < fieldCount
          ? reinterpret_cast<const WireReference*>(ptr) + refIndex
          : nullptr,
      segment, recursionLimit);
}

StructBuilder ListBuilder::getStructElementInternal(
    unsigned int index, WordCount elementSize) const {
  return StructBuilder(
      descriptor->elementDescriptor->asStruct(), segment,
      ptr + elementSize * index);
}

ListBuilder ListBuilder::initListElementInternal(unsigned int index, uint32_t size) const {
  return WireHelpers::initListReference(
      descriptor->elementDescriptor->asList(),
      reinterpret_cast<WireReference*>(ptr) + index,
      segment, size);
}

ListBuilder ListBuilder::getListElementInternal(unsigned int index) const {
  return WireHelpers::getWritableListReference(
      descriptor->elementDescriptor->asList(),
      reinterpret_cast<WireReference*>(ptr) + index,
      segment);
}

ListReader ListBuilder::asReader() const {
  return ListReader(descriptor, segment, ptr, elementCount,
      sizeInBits(descriptor->elementSize),
      descriptor->elementSize == FieldSize::STRUCT
          ? descriptor->elementDescriptor->asStruct()->fieldCount : 0,
      1 << 30);
}

StructReader ListReader::getStructElementInternal(unsigned int index) const {
  const StructDescriptor* elementDescriptor;
  if (ptr == nullptr) {
    elementDescriptor = descriptor->defaultReferences()[index]->asStruct();
  } else {
    elementDescriptor = descriptor->elementDescriptor->asStruct();

    if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
      segment->getMessage()->reportInvalidData(
          "Message is too deeply-nested or contains cycles.");
    } else {
      BitCount64 indexBit = static_cast<uint64_t>(index) * stepBits;
      return StructReader(
          elementDescriptor, segment,
          reinterpret_cast<const byte*>(ptr) + indexBit / BITS_PER_BYTE,
          descriptor->defaultData, structFieldCount, indexBit % BITS_PER_BYTE,
          recursionLimit - 1);
    }
  }

  return StructReader(elementDescriptor, segment, nullptr, descriptor->defaultData, 0, 0 * BITS, 0);
}

ListReader ListReader::getListElementInternal(unsigned int index, uint32_t size) const {
  if (ptr == nullptr) {
    return WireHelpers::readListReference(
        descriptor->defaultReferences()[index]->asList(),
        nullptr, segment, recursionLimit);
  } else {
    return WireHelpers::readListReference(
        descriptor->elementDescriptor->asList(),
        reinterpret_cast<const WireReference*>(ptr) +
            index * (stepBits / BITS_PER_WORD / WORDS_PER_REFERENCE),
        segment, recursionLimit);
  }
}

}  // namespace internal
}  // namespace capnproto
