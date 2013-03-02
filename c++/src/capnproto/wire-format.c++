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
    FAR = 3,
    RESERVED_4 = 4,
    RESERVED_5 = 5,
    RESERVED_6 = 6,
    RESERVED_7 = 7
  };

  WireValue<uint32_t> offsetAndTag;

  union {
    struct {
      WireValue<FieldNumber> fieldCount;
      WireValue<WordCount8> dataSize;
      WireValue<WireReferenceCount8> refCount;
      WireValue<uint8_t> reserved0;

      inline WordCount wordSize() const {
        return dataSize.get() + refCount.get() * WORDS_PER_REFERENCE;
      }
    } structRef;
    // Also covers capabilities.

    struct {
      WireValue<uint32_t> elementSizeAndCount;

      CAPNPROTO_ALWAYS_INLINE(FieldSize elementSize() const) {
        return static_cast<FieldSize>(elementSizeAndCount.get() >> 29);
      }
      CAPNPROTO_ALWAYS_INLINE(ElementCount elementCount() const) {
        return (elementSizeAndCount.get() & 0x1fffffffu) * ELEMENTS;
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
  CAPNPROTO_ALWAYS_INLINE(word* target()) {
    return reinterpret_cast<word*>(this) + offset();
  }
  CAPNPROTO_ALWAYS_INLINE(const word* target() const) {
    return reinterpret_cast<const word*>(this) + offset();
  }
  CAPNPROTO_ALWAYS_INLINE(Tag tag() const) {
    return static_cast<Tag>(offsetAndTag.get() & 7);
  }

  CAPNPROTO_ALWAYS_INLINE(void setTagAndOffset(Tag tag, WordCount offset)) {
    offsetAndTag.set(((offset / WORDS) << 3) | tag);
  }

  CAPNPROTO_ALWAYS_INLINE(void setStruct(
      FieldNumber fieldCount, WordCount dataSize, WireReferenceCount refCount, WordCount offset)) {
    setTagAndOffset(STRUCT, offset);
    structRef.fieldCount.set(fieldCount);
    structRef.dataSize.set(WordCount8(dataSize));
    structRef.refCount.set(refCount);
    structRef.reserved0.set(0);
  }

  CAPNPROTO_ALWAYS_INLINE(void setList(
      FieldSize elementSize, ElementCount elementCount, WordCount offset)) {
    setTagAndOffset(LIST, offset);
    CAPNPROTO_DEBUG_ASSERT(elementCount < (1 << 29) * ELEMENTS,
        "Lists are limited to 2**29 elements.");
    listRef.elementSizeAndCount.set(
        (static_cast<int>(elementSize) << 29) | (elementCount / ELEMENTS));
  }

  CAPNPROTO_ALWAYS_INLINE(void setFar(SegmentId segmentId, WordCount offset)) {
    setTagAndOffset(FAR, offset);
    farRef.segmentId.set(segmentId);
  }
};
static_assert(sizeof(WireReference) == sizeof(word),
    "capnproto::WireReference is not exactly one word.  This will probably break everything.");
static_assert(REFERENCES * WORDS_PER_REFERENCE * BYTES_PER_WORD / BYTES == sizeof(WireReference),
    "WORDS_PER_REFERENCE is wrong.");
static_assert(REFERENCES * BYTES_PER_REFERENCE / BYTES == sizeof(WireReference),
    "BYTES_PER_REFERENCE is wrong.");
static_assert(REFERENCES * BITS_PER_REFERENCE / BITS_PER_BYTE / BYTES == sizeof(WireReference),
    "BITS_PER_REFERENCE is wrong.");

// =======================================================================================

template <typename T, typename U>
static inline decltype(T() / U()) divRoundingUp(T a, U b) {
  return (a + b - 1) / b;
}

template <typename T, typename U>
static CAPNPROTO_ALWAYS_INLINE(T divRoundingUp(Quantity<T, U> a, Quantity<T, U> b));
template <typename T, typename U>
static inline T divRoundingUp(Quantity<T, U> a, Quantity<T, U> b) {
  return (a + b - unit<Quantity<T, U>>()) / b;
}

template <typename T, typename T2, typename U, typename U2>
static CAPNPROTO_ALWAYS_INLINE(
    decltype(Quantity<T, U>() / UnitRatio<T2, U, U2>())
    divRoundingUp(Quantity<T, U> a, UnitRatio<T2, U, U2> b));
template <typename T, typename T2, typename U, typename U2>
static inline decltype(Quantity<T, U>() / UnitRatio<T2, U, U2>())
divRoundingUp(Quantity<T, U> a, UnitRatio<T2, U, U2> b) {
  return (a + (unit<Quantity<T2, U2>>() * b - unit<Quantity<T2, U>>())) / b;
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
        WordCount amountPlusRef = amount + 1 * REFERENCES * WORDS_PER_REFERENCE;
        segment = segment->getMessage()->getSegmentWithAvailable(amountPlusRef);
        ptr = segment->allocate(amountPlusRef);
      } while (CAPNPROTO_EXPECT_FALSE(ptr == nullptr));

      ref->setFar(segment->getSegmentId(), segment->getOffsetTo(ptr));
      ref = reinterpret_cast<WireReference*>(ptr);

      // Allocated space follows new reference.
      return ptr + 1 * REFERENCES * WORDS_PER_REFERENCE;
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

      if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(segment->getStartPtr(),
          segment->getStartPtr() + 1 * REFERENCES * WORDS_PER_REFERENCE))) {
        return false;
      }

      ref = reinterpret_cast<const WireReference*>(segment->getStartPtr());
    }
    return true;
  }

  static CAPNPROTO_ALWAYS_INLINE(bool isStructCompatible(
      const StructDescriptor* descriptor, const WireReference* ref)) {
    if (ref->structRef.fieldCount.get() >= descriptor->fieldCount) {
      // The incoming struct has all of the fields that we know about.
      return ref->structRef.dataSize.get() >= descriptor->dataSize &&
             ref->structRef.refCount.get() >= descriptor->referenceCount;
    } else if (ref->structRef.fieldCount.get() > FieldNumber(0)) {
      // We know about more fields than the struct has, and the struct is non-empty.
      const FieldDescriptor* field = &descriptor->fields[ref->structRef.fieldCount.get().value - 1];
      return ref->structRef.dataSize.get() >= field->requiredDataSize &&
             ref->structRef.refCount.get() >= field->requiredReferenceCount;
    } else {
      // The incoming struct has no fields, so is necessarily compatible.
      return true;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(StructBuilder initStructReference(
      FieldNumber fieldCount, WordCount dataSize, WireReferenceCount referenceCount,
      WireReference* ref, SegmentBuilder* segment)) {
    if (ref->isNull()) {
      // Allocate space for the new struct.
      word* ptr = allocate(ref, segment, dataSize + referenceCount * WORDS_PER_REFERENCE);

      // Advance the pointer to point between the data and reference segments.
      ptr += dataSize;

      // Initialize the reference.
      ref->setStruct(fieldCount, dataSize, referenceCount, segment->getOffsetTo(ptr));

      // Build the StructBuilder.
      return StructBuilder(segment, ptr);
    } else {
      followFars(ref, segment);

      CAPNPROTO_ASSERT(ref->tag() == WireReference::STRUCT,
          "Called getStruct{Field,Element}() but existing reference is not a struct.");
      CAPNPROTO_ASSERT(ref->structRef.fieldCount.get() == fieldCount,
          "Trying to update struct with incorrect field count.");
      CAPNPROTO_ASSERT(ref->structRef.dataSize.get() == dataSize,
          "Trying to update struct with incorrect data size.");
      CAPNPROTO_ASSERT(ref->structRef.refCount.get() == referenceCount,
          "Trying to update struct with incorrect reference count.");

      return StructBuilder(segment, segment->getPtrUnchecked(ref->offset()));
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder initListReference(
      WireReference* ref, SegmentBuilder* segment, ElementCount elementCount,
      FieldSize elementSize)) {
    CAPNPROTO_DEBUG_ASSERT(elementSize != FieldSize::STRUCT,
        "Should have called initStructListReference() instead.");

    // Calculate size of the list.  Need to cast to uint here because a list can be up to
    // 2**32-1 bits, so int would overflow.  Plus uint division by a power of 2 is a bit shift.
    WordCount wordCount = divRoundingUp(
        elementCount * bitsPerElement(elementSize), BITS_PER_WORD);

    // Allocate the list.
    word* ptr = allocate(ref, segment, wordCount);

    // Initialize the reference.
    ref->setList(elementSize, elementCount, segment->getOffsetTo(ptr));

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, elementCount);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder initStructListReference(
      WireReference* ref, SegmentBuilder* segment, ElementCount elementCount,
      FieldNumber fieldCount, WordCount dataSize, WireReferenceCount referenceCount)) {
    auto wordsPerElement = (dataSize + referenceCount * WORDS_PER_REFERENCE) / ELEMENTS;

    // Allocate the list, prefixed by a single WireReference.
    word* ptr = allocate(ref, segment,
        1 * REFERENCES * WORDS_PER_REFERENCE + elementCount * wordsPerElement);

    // Initialize the reference.
    ref->setList(FieldSize::STRUCT, elementCount, segment->getOffsetTo(ptr));

    // The list is prefixed by a struct reference.
    WireReference* structRef = reinterpret_cast<WireReference*>(ptr);
    word* structPtr = ptr + 1 * REFERENCES * WORDS_PER_REFERENCE;
    structRef->setStruct(fieldCount, dataSize, referenceCount, segment->getOffsetTo(structPtr));

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, elementCount);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder getWritableListReference(
      FieldSize elementSize, WireReference* ref, SegmentBuilder* segment)) {
    if (ref->isNull()) {
      return ListBuilder(segment, nullptr, 0 * ELEMENTS);
    }

    followFars(ref, segment);

    CAPNPROTO_ASSERT(ref->tag() == WireReference::LIST,
        "Called getList{Field,Element}() but existing reference is not a list.");

    if (elementSize == FieldSize::STRUCT) {
      WireReference* structRef = reinterpret_cast<WireReference*>(
          segment->getPtrUnchecked(ref->offset()));
      return ListBuilder(segment,
          segment->getPtrUnchecked(structRef->offset()), ref->listRef.elementCount());
    } else {
      return ListBuilder(segment,
          segment->getPtrUnchecked(ref->offset()), ref->listRef.elementCount());
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(StructReader readStructReference(
      SegmentReader* segment, const WireReference* ref, const WireReference* defaultValue,
      int recursionLimit)) {
    const word* ptr;

    if (ref == nullptr || ref->isNull()) {
    useDefault:
      segment = nullptr;
      ref = defaultValue;
      ptr = ref->target();
    } else if (segment != nullptr) {
      if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
        segment->getMessage()->reportInvalidData(
            "Message is too deeply-nested or contains cycles.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(!followFars(ref, segment))) {
        segment->getMessage()->reportInvalidData(
            "Message contains out-of-bounds far reference.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::STRUCT)) {
        segment->getMessage()->reportInvalidData(
            "Message contains non-struct reference where struct reference was expected.");
        goto useDefault;
      }

      WordCount size = ref->structRef.dataSize.get() +
          ref->structRef.refCount.get() * WORDS_PER_REFERENCE;

      ptr = ref->target();

      if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(ptr, ptr + size))) {
        segment->getMessage()->reportInvalidData(
            "Message contained out-of-bounds struct reference.");
        goto useDefault;
      }
    }

    return StructReader(segment, ptr,
                        ref->structRef.fieldCount.get(),
                        ref->structRef.dataSize.get(),
                        ref->structRef.refCount.get(),
                        0 * BITS, recursionLimit - 1);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListReader readListReference(
      SegmentReader* segment, const WireReference* ref, const WireReference* defaultValue,
      FieldSize expectedElementSize, int recursionLimit)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      segment = nullptr;
      ref = defaultValue;
    } else if (segment != nullptr) {
      if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
        segment->getMessage()->reportInvalidData(
            "Message is too deeply-nested or contains cycles.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(!followFars(ref, segment))) {
        segment->getMessage()->reportInvalidData(
            "Message contains out-of-bounds far reference.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::LIST)) {
        segment->getMessage()->reportInvalidData(
            "Message contains non-list reference where list reference was expected.");
        goto useDefault;
      }
    }

    if (ref->listRef.elementSize() == FieldSize::STRUCT) {
      ElementCount size = ref->listRef.elementCount();
      decltype(WORDS/ELEMENTS) wordsPerElement;

      // A struct list reference actually points to a struct reference which in turn points to the
      // first struct in the list.
      const word* ptrPtr = ref->target();
      ref = reinterpret_cast<const WireReference*>(ptrPtr);

      const word* ptr;

      if (segment != nullptr) {
        if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(
            ptrPtr, ptrPtr + 1 * REFERENCES * WORDS_PER_REFERENCE))) {
          segment->getMessage()->reportInvalidData(
              "Message contains out-of-bounds list reference.");
          goto useDefault;
        }

        if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::STRUCT)) {
          segment->getMessage()->reportInvalidData(
              "Message contains struct list reference that does not point to a struct reference.");
          goto useDefault;
        }

        wordsPerElement = (ref->structRef.dataSize.get() +
            ref->structRef.refCount.get() * WORDS_PER_REFERENCE) / ELEMENTS;
        ptr = ref->target();

        if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(ptr, ptr + wordsPerElement * size))) {
          segment->getMessage()->reportInvalidData(
              "Message contained out-of-bounds struct reference.");
          goto useDefault;
        }

        // If a struct list was not expected, then presumably a non-struct list was upgraded to a
        // struct list.  We need to manipulate the pointer to point at the first field of the
        // struct.  Together with the "stepBits", this will allow the struct list to be accessed as
        // if it were a primitive list without branching.

        // Check whether the size is compatible.
        bool compatible = false;
        switch (expectedElementSize) {
          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES:
            compatible = ref->structRef.dataSize.get() > 0 * WORDS;
            break;

          case FieldSize::REFERENCE:
            ptr += ref->structRef.dataSize.get();
            compatible = ref->structRef.refCount.get() > 0 * REFERENCES;
            break;

          case FieldSize::KEY_REFERENCE:
            compatible = false;
            break;

          case FieldSize::STRUCT:
            compatible = true;
            break;
        }

        if (CAPNPROTO_EXPECT_FALSE(!compatible)) {
          segment->getMessage()->reportInvalidData("A list had incompatible element type.");
          goto useDefault;
        }

      } else {
        // This logic is equivalent to the other branch, above, but skipping all the checks.
        ptr = ref->target();
        wordsPerElement = (ref->structRef.dataSize.get() +
            ref->structRef.refCount.get() * WORDS_PER_REFERENCE) / ELEMENTS;

        if (expectedElementSize == FieldSize::REFERENCE) {
          ptr += ref->structRef.dataSize.get();
        }
      }

      return ListReader(segment, ptr, size, wordsPerElement * BITS_PER_WORD,
          ref->structRef.fieldCount.get(),
          ref->structRef.dataSize.get(),
          ref->structRef.refCount.get(),
          recursionLimit - 1);

    } else {
      // The elements of the list are NOT structs.
      decltype(BITS/ELEMENTS) step = bitsPerElement(ref->listRef.elementSize());

      const word* ptr = ref->target();

      if (segment != nullptr) {
        if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(ptr, ptr +
            divRoundingUp(ElementCount64(ref->listRef.elementCount()) * step, BITS_PER_WORD)))) {
          segment->getMessage()->reportInvalidData(
              "Message contained out-of-bounds list reference.");
          goto useDefault;
        }
      }

      if (ref->listRef.elementSize() == expectedElementSize) {
        return ListReader(segment, ptr, ref->listRef.elementCount(), step, recursionLimit - 1);
      } else if (expectedElementSize == FieldSize::STRUCT) {
        // We were expecting a struct list, but we received a list of some other type.  Perhaps a
        // non-struct list was recently upgraded to a struct list, but the sender is using the
        // old version of the protocol.  We need to verify that the struct's first field matches
        // what the sender sent us.

        WordCount dataSize;
        WireReferenceCount referenceCount;

        switch (ref->listRef.elementSize()) {
          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES:
            dataSize = 1 * WORDS;
            referenceCount = 0 * REFERENCES;
            break;

          case FieldSize::REFERENCE:
            dataSize = 0 * WORDS;
            referenceCount = 1 * REFERENCES;
            break;

          case FieldSize::KEY_REFERENCE:
            dataSize = 1 * WORDS;
            referenceCount = 1 * REFERENCES;
            break;

          case FieldSize::STRUCT:
            CAPNPROTO_ASSERT(false, "can't get here");
            break;
        }

        return ListReader(segment, ptr, ref->listRef.elementCount(), step, FieldNumber(1),
            dataSize, referenceCount, recursionLimit - 1);
      } else {
        // If segment is null, then we're parsing a trusted message that was invalid.  Crashing is
        // within contract.
        segment->getMessage()->reportInvalidData("A list had incompatible element type.");
        goto useDefault;
      }
    }
  }
};

// =======================================================================================

#if 0

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

ListReader ListReader::getListElementInternal(unsigned int index) const {
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
#endif
}  // namespace internal
}  // namespace capnproto
