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
#include <string.h>
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

  // -----------------------------------------------------------------
  // Common part of all references:  kind + offset
  //
  // Actually this is not terribly common.  The "offset" could actually be different things
  // depending on the context:
  // - For a regular (e.g. struct/list) reference, a signed word offset from the reference pointer.
  // - For an inline composite list tag (not really a reference, but structured similarly), an
  //   element count.
  // - For a FAR reference, an unsigned offset into the target segment.
  // - For a FAR landing pad, zero indicates that the target value immediately follows the pad while
  //   1 indicates that the pad is followed by another FAR reference that actually points at the
  //   value.

  enum Kind {
    STRUCT = 0,
    // Reference points at / describes a struct.

    LIST = 1,
    // Reference points at / describes a list.

    FAR = 2,
    // Reference is a "far pointer", which points at data located in a different segment.  The
    // eventual target is one of the other kinds.

    RESERVED_3 = 3
    // Reserved for future use.
  };

  WireValue<uint32_t> offsetAndKind;

  CAPNPROTO_ALWAYS_INLINE(bool isNull() const) { return offsetAndKind.get() == 0; }
  CAPNPROTO_ALWAYS_INLINE(Kind kind() const) {
    return static_cast<Kind>(offsetAndKind.get() & 3);
  }

  CAPNPROTO_ALWAYS_INLINE(word* target()) {
    return reinterpret_cast<word*>(this) + (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  CAPNPROTO_ALWAYS_INLINE(const word* target() const) {
    return reinterpret_cast<const word*>(this) + (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  CAPNPROTO_ALWAYS_INLINE(void setKindAndTarget(Kind kind, word* target)) {
    offsetAndKind.set(((target - reinterpret_cast<word*>(this)) << 2) | kind);
  }

  CAPNPROTO_ALWAYS_INLINE(ElementCount inlineCompositeListElementCount() const) {
    return (offsetAndKind.get() >> 2) * ELEMENTS;
  }
  CAPNPROTO_ALWAYS_INLINE(void setKindAndInlineCompositeListElementCount(
      Kind kind, ElementCount elementCount)) {
    offsetAndKind.set(((elementCount / ELEMENTS) << 2) | kind);
  }

  CAPNPROTO_ALWAYS_INLINE(WordCount positionInSegment() const) {
    CAPNPROTO_DEBUG_ASSERT(kind() == FAR,
        "positionInSegment() should only be called on FAR references.");
    return (offsetAndKind.get() >> 2) * WORDS;
  }
  CAPNPROTO_ALWAYS_INLINE(void setKindAndPositionInSegment(Kind kind, WordCount pos)) {
    offsetAndKind.set(((pos / WORDS) << 2) | kind);
  }

  CAPNPROTO_ALWAYS_INLINE(bool landingPadIsFollowedByAnotherReference() const) {
    return (offsetAndKind.get() & ~3) != 0;
  }
  CAPNPROTO_ALWAYS_INLINE(void setLandingPad(Kind kind, bool followedByAnotherReference)) {
    offsetAndKind.set((static_cast<uint32_t>(followedByAnotherReference) << 2) | kind);
  }

  // -----------------------------------------------------------------
  // Part of reference that depends on the kind.

  union {
    struct {
      WireValue<FieldNumber> fieldCount;
      WireValue<WordCount8> dataSize;
      WireValue<WireReferenceCount8> refCount;
      WireValue<uint8_t> reserved0;

      inline WordCount wordSize() const {
        return dataSize.get() + refCount.get() * WORDS_PER_REFERENCE;
      }

      CAPNPROTO_ALWAYS_INLINE(void set(FieldNumber fc, WordCount ds, WireReferenceCount rc)) {
        fieldCount.set(fc);
        dataSize.set(ds);
        refCount.set(rc);
        reserved0.set(0);
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
      CAPNPROTO_ALWAYS_INLINE(WordCount inlineCompositeWordCount() const) {
        return elementCount() * (1 * WORDS / ELEMENTS);
      }

      CAPNPROTO_ALWAYS_INLINE(void set(FieldSize es, ElementCount ec)) {
        CAPNPROTO_DEBUG_ASSERT(ec < (1 << 29) * ELEMENTS,
            "Lists are limited to 2**29 elements.");
        elementSizeAndCount.set((static_cast<int>(es) << 29) | (ec / ELEMENTS));
      }

      CAPNPROTO_ALWAYS_INLINE(void setInlineComposite(WordCount wc)) {
        CAPNPROTO_DEBUG_ASSERT(wc < (1 << 29) * WORDS,
            "Inline composite lists are limited to 2**29 words.");
        elementSizeAndCount.set(
            (static_cast<int>(FieldSize::INLINE_COMPOSITE) << 29) | (wc / WORDS));
      }
    } listRef;

    struct {
      WireValue<SegmentId> segmentId;

      CAPNPROTO_ALWAYS_INLINE(void set(SegmentId si)) {
        segmentId.set(si);
      }
    } farRef;
  };
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

  static CAPNPROTO_ALWAYS_INLINE(WordCount roundUpToWords(ByteCount bytes)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    uint bytes2 = bytes / BYTES;
    return ((bytes2 >> 3) + ((bytes2 & 7) != 0)) * WORDS;
  }

  static CAPNPROTO_ALWAYS_INLINE(word* allocate(
      WireReference*& ref, SegmentBuilder*& segment, WordCount amount,
      WireReference::Kind kind)) {
    word* ptr = segment->allocate(amount);

    if (ptr == nullptr) {
      // Need to allocate in a new segment.  We'll need to allocate an extra reference worth of
      // space to act as the landing pad for a far reference.

      WordCount amountPlusRef = amount + REFERENCE_SIZE_IN_WORDS;
      segment = segment->getArena()->getSegmentWithAvailable(amountPlusRef);
      ptr = segment->allocate(amountPlusRef);

      // Set up the original reference to be a far reference to the new segment.
      ref->setKindAndPositionInSegment(WireReference::FAR, segment->getOffsetTo(ptr));
      ref->farRef.set(segment->getSegmentId());

      // Initialize the landing pad to indicate that the data immediately follows the pad.
      ref = reinterpret_cast<WireReference*>(ptr);
      ref->setLandingPad(kind, false);

      // Allocated space follows new reference.
      return ptr + REFERENCE_SIZE_IN_WORDS;
    } else {
      ref->setKindAndTarget(kind, ptr);
      return ptr;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(word* followFars(WireReference*& ref, SegmentBuilder*& segment)) {
    if (ref->kind() == WireReference::FAR) {
      segment = segment->getArena()->getSegment(ref->farRef.segmentId.get());
      ref = reinterpret_cast<WireReference*>(segment->getPtrUnchecked(ref->positionInSegment()));
      if (ref->landingPadIsFollowedByAnotherReference()) {
        // Target lives elsewhere.  Another far reference follows.
        WireReference* far2 = ref + 1;
        segment = segment->getArena()->getSegment(far2->farRef.segmentId.get());
        return segment->getPtrUnchecked(far2->positionInSegment());
      } else {
        // Target immediately follows landing pad.
        return reinterpret_cast<word*>(ref + 1);
      }
    } else {
      return ref->target();
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(
      const word* followFars(const WireReference*& ref, SegmentReader*& segment)) {
    if (ref->kind() == WireReference::FAR) {
      segment = segment->getArena()->tryGetSegment(ref->farRef.segmentId.get());
      if (CAPNPROTO_EXPECT_FALSE(segment == nullptr)) {
        return nullptr;
      }

      const word* ptr = segment->getStartPtr() + ref->positionInSegment();
      if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(
          ptr, ptr + REFERENCE_SIZE_IN_WORDS))) {
        return nullptr;
      }

      ref = reinterpret_cast<const WireReference*>(ptr);

      if (ref->landingPadIsFollowedByAnotherReference()) {
        // Target is in another castle.  Another far reference follows.
        const WireReference* far2 = ref + 1;
        segment = segment->getArena()->tryGetSegment(far2->farRef.segmentId.get());
        if (CAPNPROTO_EXPECT_FALSE(segment == nullptr)) {
          return nullptr;
        }
        if (CAPNPROTO_EXPECT_FALSE(far2->kind() != WireReference::FAR)) {
          return nullptr;
        }

        ptr = segment->getStartPtr() + far2->positionInSegment();
        if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(
            ptr, ptr + REFERENCE_SIZE_IN_WORDS))) {
          return nullptr;
        }

        return ptr;
      } else {
        // Target immediately follows landing pad.
        return reinterpret_cast<const word*>(ref + 1);
      }
    } else {
      return ref->target();
    }
  }

  // -----------------------------------------------------------------

  static CAPNPROTO_ALWAYS_INLINE(
      void copyStruct(SegmentBuilder* segment, word* dst, const word* src,
                      WordCount dataSize, WireReferenceCount referenceCount)) {
    memcpy(dst, src, dataSize * BYTES_PER_WORD / BYTES);

    const WireReference* srcRefs = reinterpret_cast<const WireReference*>(src + dataSize);
    WireReference* dstRefs = reinterpret_cast<WireReference*>(dst + dataSize);

    for (uint i = 0; i < referenceCount / REFERENCES; i++) {
      SegmentBuilder* subSegment = segment;
      WireReference* dstRef = dstRefs + i;
      copyMessage(subSegment, dstRef, srcRefs + i);
    }
  }

  // Not always-inline because it's recursive.
  static word* copyMessage(
      SegmentBuilder*& segment, WireReference*& dst, const WireReference* src) {
    switch (src->kind()) {
      case WireReference::STRUCT: {
        if (src->isNull()) {
          memset(dst, 0, sizeof(WireReference));
          return nullptr;
        } else {
          const word* srcPtr = src->target();
          word* dstPtr = allocate(dst, segment, src->structRef.wordSize(), WireReference::STRUCT);

          copyStruct(segment, dstPtr, srcPtr, src->structRef.dataSize.get(),
                     src->structRef.refCount.get());

          dst->structRef.set(src->structRef.fieldCount.get(), src->structRef.dataSize.get(),
                             src->structRef.refCount.get());
          return dstPtr;
        }
      }
      case WireReference::LIST: {
        switch (src->listRef.elementSize()) {
          case FieldSize::VOID:
          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES: {
            WordCount wordCount = roundUpToWords(
                ElementCount64(src->listRef.elementCount()) *
                bitsPerElement(src->listRef.elementSize()));
            const word* srcPtr = src->target();
            word* dstPtr = allocate(dst, segment, wordCount, WireReference::LIST);
            memcpy(dstPtr, srcPtr, wordCount * BYTES_PER_WORD / BYTES);

            dst->listRef.set(src->listRef.elementSize(), src->listRef.elementCount());
            return dstPtr;
          }

          case FieldSize::REFERENCE: {
            const WireReference* srcRefs = reinterpret_cast<const WireReference*>(src->target());
            WireReference* dstRefs = reinterpret_cast<WireReference*>(
                allocate(dst, segment, src->listRef.elementCount() *
                    (1 * REFERENCES / ELEMENTS) * WORDS_PER_REFERENCE,
                    WireReference::LIST));

            uint n = src->listRef.elementCount() / ELEMENTS;
            for (uint i = 0; i < n; i++) {
              SegmentBuilder* subSegment = segment;
              WireReference* dstRef = dstRefs + i;
              copyMessage(subSegment, dstRef, srcRefs + i);
            }

            dst->listRef.set(FieldSize::REFERENCE, src->listRef.elementCount());
            return reinterpret_cast<word*>(dstRefs);
          }

          case FieldSize::INLINE_COMPOSITE: {
            const word* srcPtr = src->target();
            word* dstPtr = allocate(dst, segment,
                src->listRef.inlineCompositeWordCount() + REFERENCE_SIZE_IN_WORDS,
                WireReference::LIST);

            dst->listRef.setInlineComposite(src->listRef.inlineCompositeWordCount());

            const WireReference* srcTag = reinterpret_cast<const WireReference*>(srcPtr);
            memcpy(dstPtr, srcTag, sizeof(WireReference));

            const word* srcElement = srcPtr + REFERENCE_SIZE_IN_WORDS;
            word* dstElement = dstPtr + REFERENCE_SIZE_IN_WORDS;

            CAPNPROTO_ASSERT(srcTag->kind() == WireReference::STRUCT,
                "INLINE_COMPOSITE of lists is not yet supported.");

            uint n = srcTag->inlineCompositeListElementCount() / ELEMENTS;
            for (uint i = 0; i < n; i++) {
              copyStruct(segment, dstElement, srcElement,
                  srcTag->structRef.dataSize.get(), srcTag->structRef.refCount.get());
              srcElement += srcTag->structRef.wordSize();
              dstElement += srcTag->structRef.wordSize();
            }
            return dstPtr;
          }
        }
        break;
      }
      default:
        CAPNPROTO_ASSERT(false, "Copy source message contained unexpected kind.");
        break;
    }

    return nullptr;
  }

  // -----------------------------------------------------------------

  static CAPNPROTO_ALWAYS_INLINE(StructBuilder initStructReference(
      WireReference* ref, SegmentBuilder* segment, const word* typeDefaultValue)) {
    const WireReference* defaultRef = reinterpret_cast<const WireReference*>(typeDefaultValue);

    // Allocate space for the new struct.
    word* ptr = allocate(ref, segment, defaultRef->structRef.wordSize(), WireReference::STRUCT);

    // Copy over the data segment from the default value.  We don't have to copy the reference
    // segment because it is presumed to be all-null.
    memcpy(ptr, defaultRef->target(),
        defaultRef->structRef.dataSize.get() * BYTES_PER_WORD / BYTES);

    // Initialize the reference.
    ref->structRef.set(defaultRef->structRef.fieldCount.get(), defaultRef->structRef.dataSize.get(),
                       defaultRef->structRef.refCount.get());

    // Build the StructBuilder.
    return StructBuilder(segment, ptr,
        reinterpret_cast<WireReference*>(ptr + defaultRef->structRef.dataSize.get()));
  }

  static CAPNPROTO_ALWAYS_INLINE(StructBuilder getWritableStructReference(
      WireReference* ref, SegmentBuilder* segment, const word* defaultValue)) {
    const WireReference* defaultRef = reinterpret_cast<const WireReference*>(defaultValue);
    word* ptr;

    if (ref->isNull()) {
      ptr = copyMessage(segment, ref, defaultRef);
    } else {
      ptr = followFars(ref, segment);

      CAPNPROTO_DEBUG_ASSERT(ref->kind() == WireReference::STRUCT,
          "Called getStruct{Field,Element}() but existing reference is not a struct.");
      CAPNPROTO_DEBUG_ASSERT(
          ref->structRef.fieldCount.get() == defaultRef->structRef.fieldCount.get(),
          "Trying to update struct with incorrect field count.");
      CAPNPROTO_DEBUG_ASSERT(
          ref->structRef.dataSize.get() == defaultRef->structRef.dataSize.get(),
          "Trying to update struct with incorrect data size.");
      CAPNPROTO_DEBUG_ASSERT(
          ref->structRef.refCount.get() == defaultRef->structRef.refCount.get(),
          "Trying to update struct with incorrect reference count.");
    }

    return StructBuilder(segment, ptr,
        reinterpret_cast<WireReference*>(ptr + defaultRef->structRef.dataSize.get()));
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder initListReference(
      WireReference* ref, SegmentBuilder* segment, ElementCount elementCount,
      FieldSize elementSize)) {
    CAPNPROTO_DEBUG_ASSERT(elementSize != FieldSize::INLINE_COMPOSITE,
        "Should have called initStructListReference() instead.");

    // Calculate size of the list.
    WordCount wordCount = roundUpToWords(
        ElementCount64(elementCount) * bitsPerElement(elementSize));

    // Allocate the list.
    word* ptr = allocate(ref, segment, wordCount, WireReference::LIST);

    // Initialize the reference.
    ref->listRef.set(elementSize, elementCount);

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, elementCount);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder initStructListReference(
      WireReference* ref, SegmentBuilder* segment, ElementCount elementCount,
      const word* elementDefaultValue)) {
    const WireReference* defaultRef = reinterpret_cast<const WireReference*>(elementDefaultValue);

    auto wordsPerElement = defaultRef->structRef.wordSize() / ELEMENTS;

    // Allocate the list, prefixed by a single WireReference.
    WordCount wordCount = elementCount * wordsPerElement;
    word* ptr = allocate(ref, segment, REFERENCE_SIZE_IN_WORDS + wordCount, WireReference::LIST);

    // Initialize the reference.
    // INLINE_COMPOSITE lists replace the element count with the word count.
    ref->listRef.setInlineComposite(wordCount);

    // Initialize the list tag.
    reinterpret_cast<WireReference*>(ptr)->setKindAndInlineCompositeListElementCount(
        WireReference::STRUCT, elementCount);
    reinterpret_cast<WireReference*>(ptr)->structRef.set(
        defaultRef->structRef.fieldCount.get(),
        defaultRef->structRef.dataSize.get(),
        defaultRef->structRef.refCount.get());
    ptr += REFERENCE_SIZE_IN_WORDS;

    // Initialize the elements.  We only have to copy the data segments, as the reference segment
    // in a struct type default value is always all-null.
    ByteCount elementDataByteSize = defaultRef->structRef.dataSize.get() * BYTES_PER_WORD;
    const word* defaultData = defaultRef->target();
    word* elementPtr = ptr;
    uint n = elementCount / ELEMENTS;
    for (uint i = 0; i < n; i++) {
      memcpy(elementPtr, defaultData, elementDataByteSize / BYTES);
      elementPtr += 1 * ELEMENTS * wordsPerElement;
    }

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, elementCount);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder getWritableListReference(
      WireReference* ref, SegmentBuilder* segment, const word* defaultValue)) {
    const WireReference* defaultRef = reinterpret_cast<const WireReference*>(defaultValue);
    word* ptr;

    if (ref->isNull()) {
      if (defaultValue == nullptr) {
        return ListBuilder(segment, nullptr, 0 * ELEMENTS);
      }
      ptr = copyMessage(segment, ref, defaultRef);
    } else {
      ptr = followFars(ref, segment);

      CAPNPROTO_ASSERT(ref->kind() == WireReference::LIST,
          "Called getList{Field,Element}() but existing reference is not a list.");
    }

    if (ref->listRef.elementSize() == FieldSize::INLINE_COMPOSITE) {
      // Read the tag to get the actual element count.
      WireReference* tag = reinterpret_cast<WireReference*>(ptr);
      CAPNPROTO_ASSERT(tag->kind() == WireReference::STRUCT,
          "INLINE_COMPOSITE list with non-STRUCT elements not supported.");
      ElementCount elementCount = tag->inlineCompositeListElementCount();

      // First list element is at tag + 1 reference.
      return ListBuilder(segment, reinterpret_cast<word*>(tag + 1), elementCount);
    } else {
      return ListBuilder(segment, ptr, ref->listRef.elementCount());
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(Text::Builder initTextReference(
      WireReference* ref, SegmentBuilder* segment, ByteCount size)) {
    // The byte list must include a NUL terminator.
    ByteCount byteSize = size + 1 * BYTES;

    // Allocate the space.
    word* ptr = allocate(ref, segment, roundUpToWords(byteSize), WireReference::LIST);

    // Initialize the reference.
    ref->listRef.set(FieldSize::BYTE, byteSize * (1 * ELEMENTS / BYTES));

    // Build the Text::Builder.  This will initialize the NUL terminator.
    return Text::Builder(reinterpret_cast<char*>(ptr), size / BYTES);
  }

  static CAPNPROTO_ALWAYS_INLINE(void setTextReference(
      WireReference* ref, SegmentBuilder* segment, Text::Reader value)) {
    initTextReference(ref, segment, value.size() * BYTES).copyFrom(value);
  }

  static CAPNPROTO_ALWAYS_INLINE(Text::Builder getWritableTextReference(
      WireReference* ref, SegmentBuilder* segment,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref->isNull()) {
      Text::Builder builder = initTextReference(ref, segment, defaultSize);
      builder.copyFrom(defaultValue);
      return builder;
    } else {
      word* ptr = followFars(ref, segment);

      CAPNPROTO_ASSERT(ref->kind() == WireReference::LIST,
          "Called getText{Field,Element}() but existing reference is not a list.");
      CAPNPROTO_ASSERT(ref->listRef.elementSize() == FieldSize::BYTE,
          "Called getText{Field,Element}() but existing list reference is not byte-sized.");

      // Subtract 1 from the size for the NUL terminator.
      return Text::Builder(reinterpret_cast<char*>(ptr), ref->listRef.elementCount() / ELEMENTS - 1);
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(Data::Builder initDataReference(
      WireReference* ref, SegmentBuilder* segment, ByteCount size)) {
    // Allocate the space.
    word* ptr = allocate(ref, segment, roundUpToWords(size), WireReference::LIST);

    // Initialize the reference.
    ref->listRef.set(FieldSize::BYTE, size * (1 * ELEMENTS / BYTES));

    // Build the Data::Builder.
    return Data::Builder(reinterpret_cast<char*>(ptr), size / BYTES);
  }

  static CAPNPROTO_ALWAYS_INLINE(void setDataReference(
      WireReference* ref, SegmentBuilder* segment, Data::Reader value)) {
    initDataReference(ref, segment, value.size() * BYTES).copyFrom(value);
  }

  static CAPNPROTO_ALWAYS_INLINE(Data::Builder getWritableDataReference(
      WireReference* ref, SegmentBuilder* segment,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref->isNull()) {
      Data::Builder builder = initDataReference(ref, segment, defaultSize);
      builder.copyFrom(defaultValue);
      return builder;
    } else {
      word* ptr = followFars(ref, segment);

      CAPNPROTO_ASSERT(ref->kind() == WireReference::LIST,
          "Called getData{Field,Element}() but existing reference is not a list.");
      CAPNPROTO_ASSERT(ref->listRef.elementSize() == FieldSize::BYTE,
          "Called getData{Field,Element}() but existing list reference is not byte-sized.");

      return Data::Builder(reinterpret_cast<char*>(ptr), ref->listRef.elementCount() / ELEMENTS);
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(StructReader readStructReference(
      SegmentReader* segment, const WireReference* ref, const word* defaultValue,
      int nestingLimit)) {
    const word* ptr;

    if (ref == nullptr || ref->isNull()) {
    useDefault:
      segment = nullptr;
      ref = reinterpret_cast<const WireReference*>(defaultValue);
      ptr = ref->target();
    } else if (segment != nullptr) {
      if (CAPNPROTO_EXPECT_FALSE(nestingLimit == 0)) {
        segment->getArena()->reportInvalidData(
            "Message is too deeply-nested or contains cycles.  See capnproto::ReadOptions.");
        goto useDefault;
      }

      ptr = followFars(ref, segment);
      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        segment->getArena()->reportInvalidData(
            "Message contains invalid far reference.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->kind() != WireReference::STRUCT)) {
        segment->getArena()->reportInvalidData(
            "Message contains non-struct reference where struct reference was expected.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(ptr, ptr + ref->structRef.wordSize()))){
        segment->getArena()->reportInvalidData(
            "Message contained out-of-bounds struct reference.");
        goto useDefault;
      }
    } else {
      // Trusted messages don't contain far pointers.
      ptr = ref->target();
    }

    return StructReader(
        segment, ptr, reinterpret_cast<const WireReference*>(ptr + ref->structRef.dataSize.get()),
        ref->structRef.fieldCount.get(),
        ref->structRef.dataSize.get(),
        ref->structRef.refCount.get(),
        0 * BITS, nestingLimit - 1);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListReader readListReference(
      SegmentReader* segment, const WireReference* ref, const word* defaultValue,
      FieldSize expectedElementSize, int nestingLimit)) {
    const word* ptr;
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr) {
        return ListReader(nullptr, nullptr, 0 * ELEMENTS, 0 * BITS / ELEMENTS, nestingLimit - 1);
      }
      segment = nullptr;
      ref = reinterpret_cast<const WireReference*>(defaultValue);
      ptr = ref->target();
    } else if (segment != nullptr) {
      if (CAPNPROTO_EXPECT_FALSE(nestingLimit == 0)) {
        segment->getArena()->reportInvalidData(
            "Message is too deeply-nested or contains cycles.  See capnproto::ReadOptions.");
        goto useDefault;
      }

      ptr = followFars(ref, segment);
      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        segment->getArena()->reportInvalidData(
            "Message contains invalid far reference.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->kind() != WireReference::LIST)) {
        segment->getArena()->reportInvalidData(
            "Message contains non-list reference where list reference was expected.");
        goto useDefault;
      }
    } else {
      // Trusted messages don't contain far pointers.
      ptr = ref->target();
    }

    if (ref->listRef.elementSize() == FieldSize::INLINE_COMPOSITE) {
      decltype(WORDS/ELEMENTS) wordsPerElement;
      ElementCount size;

      WordCount wordCount = ref->listRef.inlineCompositeWordCount();

      // An INLINE_COMPOSITE list points to a tag, which is formatted like a reference.
      const WireReference* tag = reinterpret_cast<const WireReference*>(ptr);
      ptr += REFERENCE_SIZE_IN_WORDS;

      if (segment != nullptr) {
        if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(
            ptr - REFERENCE_SIZE_IN_WORDS, ptr + wordCount))) {
          segment->getArena()->reportInvalidData(
              "Message contains out-of-bounds list reference.");
          goto useDefault;
        }

        if (CAPNPROTO_EXPECT_FALSE(tag->kind() != WireReference::STRUCT)) {
          segment->getArena()->reportInvalidData(
              "INLINE_COMPOSITE lists of non-STRUCT type are not supported.");
          goto useDefault;
        }

        size = tag->inlineCompositeListElementCount();
        wordsPerElement = tag->structRef.wordSize() / ELEMENTS;

        if (CAPNPROTO_EXPECT_FALSE(size * wordsPerElement > wordCount)) {
          segment->getArena()->reportInvalidData(
              "INLINE_COMPOSITE list's elements overrun its word count.");
          goto useDefault;
        }

        // If a struct list was not expected, then presumably a non-struct list was upgraded to a
        // struct list.  We need to manipulate the pointer to point at the first field of the
        // struct.  Together with the "stepBits", this will allow the struct list to be accessed as
        // if it were a primitive list without branching.

        // Check whether the size is compatible.
        bool compatible = false;
        switch (expectedElementSize) {
          case FieldSize::VOID:
            compatible = true;
            break;

          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES:
            compatible = tag->structRef.dataSize.get() > 0 * WORDS;
            break;

          case FieldSize::REFERENCE:
            // We expected a list of references but got a list of structs.  Assuming the first field
            // in the struct is the reference we were looking for, we want to munge the pointer to
            // point at the first element's reference segment.
            ptr += tag->structRef.dataSize.get();
            compatible = tag->structRef.refCount.get() > 0 * REFERENCES;
            break;

          case FieldSize::INLINE_COMPOSITE:
            compatible = true;
            break;
        }

        if (CAPNPROTO_EXPECT_FALSE(!compatible)) {
          segment->getArena()->reportInvalidData("A list had incompatible element type.");
          goto useDefault;
        }

      } else {
        // Trusted message.
        // This logic is equivalent to the other branch, above, but skipping all the checks.
        size = tag->inlineCompositeListElementCount();
        wordsPerElement = tag->structRef.wordSize() / ELEMENTS;

        if (expectedElementSize == FieldSize::REFERENCE) {
          ptr += tag->structRef.dataSize.get();
        }
      }

      return ListReader(segment, ptr, size, wordsPerElement * BITS_PER_WORD,
          tag->structRef.fieldCount.get(),
          tag->structRef.dataSize.get(),
          tag->structRef.refCount.get(),
          nestingLimit - 1);

    } else {
      // The elements of the list are NOT structs.
      decltype(BITS/ELEMENTS) step = bitsPerElement(ref->listRef.elementSize());

      if (segment != nullptr) {
        if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(ptr, ptr +
            roundUpToWords(ElementCount64(ref->listRef.elementCount()) * step)))) {
          segment->getArena()->reportInvalidData(
              "Message contained out-of-bounds list reference.");
          goto useDefault;
        }
      }

      if (ref->listRef.elementSize() == expectedElementSize) {
        return ListReader(segment, ptr, ref->listRef.elementCount(), step, nestingLimit - 1);
      } else if (expectedElementSize == FieldSize::INLINE_COMPOSITE) {
        // We were expecting a struct list, but we received a list of some other type.  Perhaps a
        // non-struct list was recently upgraded to a struct list, but the sender is using the
        // old version of the protocol.  We need to verify that the struct's first field matches
        // what the sender sent us.

        WordCount dataSize;
        WireReferenceCount referenceCount;

        switch (ref->listRef.elementSize()) {
          case FieldSize::VOID:
            dataSize = 0 * WORDS;
            referenceCount = 0 * REFERENCES;
            break;

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

          case FieldSize::INLINE_COMPOSITE:
            CAPNPROTO_ASSERT(false, "can't get here");
            break;
        }

        return ListReader(segment, ptr, ref->listRef.elementCount(), step, FieldNumber(1),
            dataSize, referenceCount, nestingLimit - 1);
      } else {
        CAPNPROTO_ASSERT(segment != nullptr, "Trusted message had incompatible list element type.");
        segment->getArena()->reportInvalidData("A list had incompatible element type.");
        goto useDefault;
      }
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(Text::Reader readTextReference(
      SegmentReader* segment, const WireReference* ref,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr) {
        defaultValue = "";
      }
      return Text::Reader(reinterpret_cast<const char*>(defaultValue), defaultSize / BYTES);
    } else if (segment == nullptr) {
      // Trusted message.
      return Text::Reader(reinterpret_cast<const char*>(ref->target()),
                          ref->listRef.elementCount() / ELEMENTS - 1);
    } else {
      const word* ptr = followFars(ref, segment);
      uint size = ref->listRef.elementCount() / ELEMENTS;

      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        segment->getArena()->reportInvalidData(
            "Message contains invalid far reference.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->kind() != WireReference::LIST)) {
        segment->getArena()->reportInvalidData(
            "Message contains non-list reference where text was expected.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->listRef.elementSize() != FieldSize::BYTE)) {
        segment->getArena()->reportInvalidData(
            "Message contains list reference of non-bytes where text was expected.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(ptr, ptr +
          roundUpToWords(ref->listRef.elementCount() * (1 * BYTES / ELEMENTS))))) {
        segment->getArena()->reportInvalidData(
            "Message contained out-of-bounds text reference.");
        goto useDefault;
      }

      const char* cptr = reinterpret_cast<const char*>(ptr);
      --size;  // NUL terminator

      if (CAPNPROTO_EXPECT_FALSE(cptr[size] != '\0')) {
        segment->getArena()->reportInvalidData(
            "Message contains text that is not NUL-terminated.");
        goto useDefault;
      }

      return Text::Reader(cptr, size);
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(Data::Reader readDataReference(
      SegmentReader* segment, const WireReference* ref,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      return Data::Reader(reinterpret_cast<const char*>(defaultValue), defaultSize / BYTES);
    } else if (segment == nullptr) {
      // Trusted message.
      return Data::Reader(reinterpret_cast<const char*>(ref->target()),
                        ref->listRef.elementCount() / ELEMENTS);
    } else {
      const word* ptr = followFars(ref, segment);
      uint size = ref->listRef.elementCount() / ELEMENTS;

      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        segment->getArena()->reportInvalidData(
            "Message contains invalid far reference.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->kind() != WireReference::LIST)) {
        segment->getArena()->reportInvalidData(
            "Message contains non-list reference where data was expected.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->listRef.elementSize() != FieldSize::BYTE)) {
        segment->getArena()->reportInvalidData(
            "Message contains list reference of non-bytes where data was expected.");
        goto useDefault;
      }

      if (CAPNPROTO_EXPECT_FALSE(!segment->containsInterval(ptr, ptr +
          roundUpToWords(ref->listRef.elementCount() * (1 * BYTES / ELEMENTS))))) {
        segment->getArena()->reportInvalidData(
            "Message contained out-of-bounds data reference.");
        goto useDefault;
      }

      return Data::Reader(reinterpret_cast<const char*>(ptr), size);
    }
  }
};

// =======================================================================================

StructBuilder StructBuilder::initRoot(
    SegmentBuilder* segment, word* location, const word* defaultValue) {
  return WireHelpers::initStructReference(
      reinterpret_cast<WireReference*>(location), segment, defaultValue);
}

StructBuilder StructBuilder::getRoot(
    SegmentBuilder* segment, word* location, const word* defaultValue) {
  return WireHelpers::getWritableStructReference(
      reinterpret_cast<WireReference*>(location), segment, defaultValue);
}

StructBuilder StructBuilder::initStructField(
    WireReferenceCount refIndex, const word* typeDefaultValue) const {
  return WireHelpers::initStructReference(references + refIndex, segment, typeDefaultValue);
}

StructBuilder StructBuilder::getStructField(
    WireReferenceCount refIndex, const word* defaultValue) const {
  return WireHelpers::getWritableStructReference(references + refIndex, segment, defaultValue);
}

ListBuilder StructBuilder::initListField(
    WireReferenceCount refIndex, FieldSize elementSize, ElementCount elementCount) const {
  return WireHelpers::initListReference(
      references + refIndex, segment,
      elementCount, elementSize);
}

ListBuilder StructBuilder::initStructListField(
    WireReferenceCount refIndex, ElementCount elementCount,
    const word* elementDefaultValue) const {
  return WireHelpers::initStructListReference(
      references + refIndex, segment, elementCount, elementDefaultValue);
}

ListBuilder StructBuilder::getListField(
    WireReferenceCount refIndex, const word* defaultValue) const {
  return WireHelpers::getWritableListReference(
      references + refIndex, segment, defaultValue);
}

Text::Builder StructBuilder::initTextField(WireReferenceCount refIndex, ByteCount size) const {
  return WireHelpers::initTextReference(references + refIndex, segment, size);
}
void StructBuilder::setTextField(WireReferenceCount refIndex, Text::Reader value) const {
  WireHelpers::setTextReference(references + refIndex, segment, value);
}
Text::Builder StructBuilder::getTextField(
    WireReferenceCount refIndex, const void* defaultValue, ByteCount defaultSize) const {
  return WireHelpers::getWritableTextReference(
      references + refIndex, segment, defaultValue, defaultSize);
}

Data::Builder StructBuilder::initDataField(WireReferenceCount refIndex, ByteCount size) const {
  return WireHelpers::initDataReference(references + refIndex, segment, size);
}
void StructBuilder::setDataField(WireReferenceCount refIndex, Data::Reader value) const {
  WireHelpers::setDataReference(references + refIndex, segment, value);
}
Data::Builder StructBuilder::getDataField(
    WireReferenceCount refIndex, const void* defaultValue, ByteCount defaultSize) const {
  return WireHelpers::getWritableDataReference(
      references + refIndex, segment, defaultValue, defaultSize);
}

StructReader StructBuilder::asReader() const {
  // HACK:  We just give maxed-out field, data size, and reference counts because they are only
  // used for checking for field presence.
  static_assert(sizeof(WireReference::structRef.fieldCount) == 1,
      "Has the maximum field count changed?");
  static_assert(sizeof(WireReference::structRef.dataSize) == 1,
      "Has the maximum data size changed?");
  static_assert(sizeof(WireReference::structRef.refCount) == 1,
      "Has the maximum reference count changed?");
  return StructReader(segment, data, references,
      FieldNumber(0xff), 0xff * WORDS, 0xff * REFERENCES,
      0 * BITS, std::numeric_limits<int>::max());
}

StructReader StructReader::readRootTrusted(const word* location, const word* defaultValue) {
  return WireHelpers::readStructReference(nullptr, reinterpret_cast<const WireReference*>(location),
                                          defaultValue, std::numeric_limits<int>::max());
}

StructReader StructReader::readRoot(const word* location, const word* defaultValue,
                                    SegmentReader* segment, int nestingLimit) {
  if (!segment->containsInterval(location, location + REFERENCE_SIZE_IN_WORDS)) {
    segment->getArena()->reportInvalidData("Root location out-of-bounds.");
    location = nullptr;
  }

  return WireHelpers::readStructReference(segment, reinterpret_cast<const WireReference*>(location),
                                          defaultValue, nestingLimit);
}

StructReader StructReader::getStructField(
    WireReferenceCount refIndex, const word* defaultValue) const {
  const WireReference* ref = refIndex >= referenceCount ? nullptr : references + refIndex;
  return WireHelpers::readStructReference(segment, ref, defaultValue, nestingLimit);
}

ListReader StructReader::getListField(
    WireReferenceCount refIndex, FieldSize expectedElementSize, const word* defaultValue) const {
  const WireReference* ref = refIndex >= referenceCount ? nullptr : references + refIndex;
  return WireHelpers::readListReference(
      segment, ref, defaultValue, expectedElementSize, nestingLimit);
}

Text::Reader StructReader::getTextField(
    WireReferenceCount refIndex, const void* defaultValue, ByteCount defaultSize) const {
  const WireReference* ref = refIndex >= referenceCount ? nullptr : references + refIndex;
  return WireHelpers::readTextReference(segment, ref, defaultValue, defaultSize);
}

Data::Reader StructReader::getDataField(
    WireReferenceCount refIndex, const void* defaultValue, ByteCount defaultSize) const {
  const WireReference* ref = refIndex >= referenceCount ? nullptr : references + refIndex;
  return WireHelpers::readDataReference(segment, ref, defaultValue, defaultSize);
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
    WireReferenceCount index, ElementCount elementCount, const word* elementDefaultValue) const {
  return WireHelpers::initStructListReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment,
      elementCount, elementDefaultValue);
}

ListBuilder ListBuilder::getListElement(WireReferenceCount index) const {
  return WireHelpers::getWritableListReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment, nullptr);
}

Text::Builder ListBuilder::initTextElement(WireReferenceCount index, ByteCount size) const {
  return WireHelpers::initTextReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment, size);
}
void ListBuilder::setTextElement(WireReferenceCount index, Text::Reader value) const {
  WireHelpers::setTextReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment, value);
}
Text::Builder ListBuilder::getTextElement(WireReferenceCount index) const {
  return WireHelpers::getWritableTextReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment, "", 0 * BYTES);
}

Data::Builder ListBuilder::initDataElement(WireReferenceCount index, ByteCount size) const {
  return WireHelpers::initDataReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment, size);
}
void ListBuilder::setDataElement(WireReferenceCount index, Data::Reader value) const {
  WireHelpers::setDataReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment, value);
}
Data::Builder ListBuilder::getDataElement(WireReferenceCount index) const {
  return WireHelpers::getWritableDataReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment, nullptr, 0 * BYTES);
}

ListReader ListBuilder::asReader(FieldSize elementSize) const {
  // TODO:  For INLINE_COMPOSITE I suppose we could just check the tag?
  CAPNPROTO_ASSERT(elementSize != FieldSize::INLINE_COMPOSITE,
      "Need to call the other asReader() overload for INLINE_COMPOSITE lists.");
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
  if (CAPNPROTO_EXPECT_FALSE((segment != nullptr) & (nestingLimit == 0))) {
    segment->getArena()->reportInvalidData(
        "Message is too deeply-nested or contains cycles.  See capnproto::ReadOptions.");
    return WireHelpers::readStructReference(nullptr, nullptr, defaultValue, nestingLimit);
  } else {
    BitCount64 indexBit = ElementCount64(index) * stepBits;
    const byte* structPtr = reinterpret_cast<const byte*>(ptr) + indexBit / BITS_PER_BYTE;
    return StructReader(
        segment, structPtr,
        reinterpret_cast<const WireReference*>(structPtr + structDataSize * BYTES_PER_WORD),
        structFieldCount, structDataSize, structReferenceCount, indexBit % BITS_PER_BYTE,
        nestingLimit - 1);
  }
}

ListReader ListReader::getListElement(
    WireReferenceCount index, FieldSize expectedElementSize) const {
  return WireHelpers::readListReference(
      segment, reinterpret_cast<const WireReference*>(ptr) + index,
      nullptr, expectedElementSize, nestingLimit);
}

Text::Reader ListReader::getTextElement(WireReferenceCount index) const {
  return WireHelpers::readTextReference(segment,
      reinterpret_cast<const WireReference*>(ptr) + index, "", 0 * BYTES);
}

Data::Reader ListReader::getDataElement(WireReferenceCount index) const {
  return WireHelpers::readDataReference(segment,
      reinterpret_cast<const WireReference*>(ptr) + index, nullptr, 0 * BYTES);
}

}  // namespace internal
}  // namespace capnproto
