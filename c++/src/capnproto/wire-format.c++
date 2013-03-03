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

struct WireHelpers {
  static CAPNPROTO_ALWAYS_INLINE(WordCount roundUpToWords(BitCount64 bits)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    uint64_t bits2 = bits / BITS;
    return ((bits2 >> 6) + ((bits2 & 63) != 0)) * WORDS;
  }

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

  static CAPNPROTO_ALWAYS_INLINE(StructBuilder initStructReference(
      FieldNumber fieldCount, WordCount dataSize, WireReferenceCount referenceCount,
      WireReference* ref, SegmentBuilder* segment)) {
    if (ref->isNull()) {
      // Allocate space for the new struct.
      word* ptr = allocate(ref, segment, dataSize + referenceCount * WORDS_PER_REFERENCE);

      // Initialize the reference.
      ref->setStruct(fieldCount, dataSize, referenceCount, segment->getOffsetTo(ptr));

      // Build the StructBuilder.
      return StructBuilder(segment, ptr, reinterpret_cast<WireReference*>(ptr + dataSize));
    } else {
      followFars(ref, segment);

      CAPNPROTO_DEBUG_ASSERT(ref->tag() == WireReference::STRUCT,
          "Called getStruct{Field,Element}() but existing reference is not a struct.");
      CAPNPROTO_DEBUG_ASSERT(ref->structRef.fieldCount.get() == fieldCount,
          "Trying to update struct with incorrect field count.");
      CAPNPROTO_DEBUG_ASSERT(ref->structRef.dataSize.get() == dataSize,
          "Trying to update struct with incorrect data size.");
      CAPNPROTO_DEBUG_ASSERT(ref->structRef.refCount.get() == referenceCount,
          "Trying to update struct with incorrect reference count.");

      word* ptr = segment->getPtrUnchecked(ref->offset());
      return StructBuilder(segment, ptr, reinterpret_cast<WireReference*>(ptr + dataSize));
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder initListReference(
      WireReference* ref, SegmentBuilder* segment, ElementCount elementCount,
      FieldSize elementSize)) {
    CAPNPROTO_DEBUG_ASSERT(elementSize != FieldSize::STRUCT,
        "Should have called initStructListReference() instead.");

    // Calculate size of the list.
    WordCount wordCount = roundUpToWords(
        ElementCount64(elementCount) * bitsPerElement(elementSize));

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
      SegmentReader* segment, const WireReference* ref, const word* defaultValue,
      int recursionLimit)) {
    const word* ptr = ref->target();

    if (ref == nullptr || ref->isNull()) {
    useDefault:
      segment = nullptr;
      ref = reinterpret_cast<const WireReference*>(defaultValue);
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
      SegmentReader* segment, const WireReference* ref, const word* defaultValue,
      FieldSize expectedElementSize, int recursionLimit)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      segment = nullptr;
      ref = reinterpret_cast<const WireReference*>(defaultValue);
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
            roundUpToWords(ElementCount64(ref->listRef.elementCount()) * step)))) {
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

StructBuilder StructBuilder::initRoot(SegmentBuilder* segment, word* location,
    FieldNumber fieldCount, WordCount dataSize, WireReferenceCount referenceCount) {
  return WireHelpers::initStructReference(
      fieldCount, dataSize, referenceCount,
      reinterpret_cast<WireReference*>(location), segment);
}

StructBuilder StructBuilder::getStructField(
    WireReferenceCount refIndex, FieldNumber fieldCount,
    WordCount dataSize, WireReferenceCount referenceCount) const {
  return WireHelpers::initStructReference(
      fieldCount, dataSize, referenceCount,
      references + refIndex, segment);
}

ListBuilder StructBuilder::initListField(
    WireReferenceCount refIndex, FieldSize elementSize, ElementCount elementCount) const {
  return WireHelpers::initListReference(
      references + refIndex, segment,
      elementCount, elementSize);
}

ListBuilder StructBuilder::initStructListField(
    WireReferenceCount refIndex, ElementCount elementCount,
    FieldNumber fieldCount, WordCount dataSize, WireReferenceCount referenceCount) const {
  return WireHelpers::initStructListReference(
      references + refIndex, segment,
      elementCount, fieldCount, dataSize, referenceCount);
}

ListBuilder StructBuilder::getListField(WireReferenceCount refIndex, FieldSize elementSize) const {
  return WireHelpers::getWritableListReference(
      elementSize, references + refIndex, segment);
}

StructReader StructBuilder::asReader() const {
  // HACK:  We just give maxed-out field and reference counts because they are only used for
  // checking for field presence.
  static_assert(sizeof(WireReference::structRef.fieldCount) == 1,
      "Has the maximum field count changed?");
  static_assert(sizeof(WireReference::structRef.refCount) == 1,
      "Has the maximum reference count changed?");
  return StructReader(segment, data, FieldNumber(0xff),
      intervalLength(data, reinterpret_cast<word*>(references)), 0xff * REFERENCES,
      0 * BITS, std::numeric_limits<int>::max());
}

StructReader StructReader::readRootTrusted(const word* location, const word* defaultValue) {
  return WireHelpers::readStructReference(nullptr, reinterpret_cast<const WireReference*>(location),
                                          defaultValue, std::numeric_limits<int>::max());
}

StructReader StructReader::readRoot(const word* location, const word* defaultValue,
                                    SegmentReader* segment, int recursionLimit) {
  if (segment->containsInterval(location, location + 1 * REFERENCES * WORDS_PER_REFERENCE)) {
    segment->getMessage()->reportInvalidData("Root location out-of-bounds.");
    location = nullptr;
  }

  return WireHelpers::readStructReference(segment, reinterpret_cast<const WireReference*>(location),
                                          defaultValue, recursionLimit);
}

StructReader StructReader::getStructField(
    WireReferenceCount refIndex, const word* defaultValue) const {
  const WireReference* ref = refIndex >= referenceCount ? nullptr :
      reinterpret_cast<const WireReference*>(
          reinterpret_cast<const word*>(ptr) + dataSize) + refIndex;
  return WireHelpers::readStructReference(segment, ref, defaultValue, recursionLimit);
}

ListReader StructReader::getListField(
    WireReferenceCount refIndex, FieldSize expectedElementSize, const word* defaultValue) const {
  const WireReference* ref = refIndex >= referenceCount ? nullptr :
      reinterpret_cast<const WireReference*>(
          reinterpret_cast<const word*>(ptr) + dataSize) + refIndex;
  return WireHelpers::readListReference(
      segment, ref, defaultValue, expectedElementSize, recursionLimit);
}

StructBuilder ListBuilder::getStructElement(
    ElementCount index, decltype(WORDS/ELEMENTS) elementSize, WordCount structDataSize) const {
  word* structPtr = ptr + elementSize * index;
  return StructBuilder(segment, structPtr,
      reinterpret_cast<WireReference*>(structPtr + structDataSize));
}

ListBuilder ListBuilder::initListElement(
    WireReferenceCount index, FieldSize elementSize, ElementCount elementCount) const {
  return WireHelpers::initListReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment,
      elementCount, elementSize);
}

ListBuilder ListBuilder::initStructListElement(
    WireReferenceCount index, ElementCount elementCount,
    FieldNumber fieldCount, WordCount dataSize, WireReferenceCount referenceCount) const {
  return WireHelpers::initStructListReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment,
      elementCount, fieldCount, dataSize, referenceCount);
}

ListBuilder ListBuilder::getListElement(WireReferenceCount index, FieldSize elementSize) const {
  return WireHelpers::getWritableListReference(
      elementSize, reinterpret_cast<WireReference*>(ptr) + index, segment);
}

ListReader ListBuilder::asReader(FieldSize elementSize) const {
  return ListReader(segment, ptr, elementCount, bitsPerElement(elementSize),
                    std::numeric_limits<int>::max());
}

ListReader ListBuilder::asReader(FieldNumber fieldCount, WordCount dataSize,
                                 WireReferenceCount referenceCount) const {
  return ListReader(segment, ptr, elementCount,
      (dataSize + referenceCount * WORDS_PER_REFERENCE) * BITS_PER_WORD / ELEMENTS,
      fieldCount, dataSize, referenceCount, std::numeric_limits<int>::max());
}

StructReader ListReader::getStructElement(ElementCount index, const word* defaultValue) const {
  if (CAPNPROTO_EXPECT_FALSE((segment != nullptr) & (recursionLimit == 0))) {
    segment->getMessage()->reportInvalidData(
        "Message is too deeply-nested or contains cycles.");
    return WireHelpers::readStructReference(nullptr, nullptr, defaultValue, recursionLimit);
  } else {
    BitCount64 indexBit = ElementCount64(index) * stepBits;
    return StructReader(
        segment, reinterpret_cast<const byte*>(ptr) + indexBit / BITS_PER_BYTE,
        structFieldCount, structDataSize, structReferenceCount, indexBit % BITS_PER_BYTE,
        recursionLimit - 1);
  }
}

ListReader ListReader::getListElement(
    WireReferenceCount index, FieldSize expectedElementSize, const word* defaultValue) const {
  return WireHelpers::readListReference(
      segment, reinterpret_cast<const WireReference*>(ptr) + index,
      defaultValue, expectedElementSize, recursionLimit);
}

}  // namespace internal
}  // namespace capnproto
