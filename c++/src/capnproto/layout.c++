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

#define CAPNPROTO_PRIVATE
#include "layout.h"
#include "logging.h"
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
  // - For a regular (e.g. struct/list) reference, a signed word offset from the word immediately
  //   following the reference pointer.  (The off-by-one means the offset is more often zero, saving
  //   bytes on the wire when packed.)
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

  CAPNPROTO_ALWAYS_INLINE(Kind kind() const) {
    return static_cast<Kind>(offsetAndKind.get() & 3);
  }

  CAPNPROTO_ALWAYS_INLINE(word* target()) {
    return reinterpret_cast<word*>(this) + 1 + (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  CAPNPROTO_ALWAYS_INLINE(const word* target() const) {
    return reinterpret_cast<const word*>(this) + 1 +
        (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  CAPNPROTO_ALWAYS_INLINE(void setKindAndTarget(Kind kind, word* target)) {
    offsetAndKind.set(((target - reinterpret_cast<word*>(this) - 1) << 2) | kind);
  }

  CAPNPROTO_ALWAYS_INLINE(ElementCount inlineCompositeListElementCount() const) {
    return (offsetAndKind.get() >> 2) * ELEMENTS;
  }
  CAPNPROTO_ALWAYS_INLINE(void setKindAndInlineCompositeListElementCount(
      Kind kind, ElementCount elementCount)) {
    offsetAndKind.set(((elementCount / ELEMENTS) << 2) | kind);
  }

  CAPNPROTO_ALWAYS_INLINE(WordCount farPositionInSegment() const) {
    DPRECOND(kind() == FAR,
        "positionInSegment() should only be called on FAR references.");
    return (offsetAndKind.get() >> 3) * WORDS;
  }
  CAPNPROTO_ALWAYS_INLINE(bool isDoubleFar() const) {
    DPRECOND(kind() == FAR,
        "isDoubleFar() should only be called on FAR references.");
    return (offsetAndKind.get() >> 2) & 1;
  }
  CAPNPROTO_ALWAYS_INLINE(void setFar(bool isDoubleFar, WordCount pos)) {
    offsetAndKind.set(((pos / WORDS) << 3) | (static_cast<uint32_t>(isDoubleFar) << 2) |
                      static_cast<uint32_t>(Kind::FAR));
  }

  // -----------------------------------------------------------------
  // Part of reference that depends on the kind.

  union {
    uint32_t upper32Bits;

    struct {
      WireValue<WordCount16> dataSize;
      WireValue<WireReferenceCount16> refCount;

      inline WordCount wordSize() const {
        return dataSize.get() + refCount.get() * WORDS_PER_REFERENCE;
      }

      CAPNPROTO_ALWAYS_INLINE(void set(WordCount ds, WireReferenceCount rc)) {
        dataSize.set(ds);
        refCount.set(rc);
      }
      CAPNPROTO_ALWAYS_INLINE(void set(StructSize size)) {
        dataSize.set(size.data);
        refCount.set(size.pointers);
      }
    } structRef;
    // Also covers capabilities.

    struct {
      WireValue<uint32_t> elementSizeAndCount;

      CAPNPROTO_ALWAYS_INLINE(FieldSize elementSize() const) {
        return static_cast<FieldSize>(elementSizeAndCount.get() & 7);
      }
      CAPNPROTO_ALWAYS_INLINE(ElementCount elementCount() const) {
        return (elementSizeAndCount.get() >> 3) * ELEMENTS;
      }
      CAPNPROTO_ALWAYS_INLINE(WordCount inlineCompositeWordCount() const) {
        return elementCount() * (1 * WORDS / ELEMENTS);
      }

      CAPNPROTO_ALWAYS_INLINE(void set(FieldSize es, ElementCount ec)) {
        DPRECOND(ec < (1 << 29) * ELEMENTS, "Lists are limited to 2**29 elements.");
        elementSizeAndCount.set(((ec / ELEMENTS) << 3) | static_cast<int>(es));
      }

      CAPNPROTO_ALWAYS_INLINE(void setInlineComposite(WordCount wc)) {
        DPRECOND(wc < (1 << 29) * WORDS, "Inline composite lists are limited to 2**29 words.");
        elementSizeAndCount.set(((wc / WORDS) << 3) |
                                static_cast<int>(FieldSize::INLINE_COMPOSITE));
      }
    } listRef;

    struct {
      WireValue<SegmentId> segmentId;

      CAPNPROTO_ALWAYS_INLINE(void set(SegmentId si)) {
        segmentId.set(si);
      }
    } farRef;
  };

  CAPNPROTO_ALWAYS_INLINE(bool isNull() const) {
    // If the upper 32 bits are zero, this is a pointer to an empty struct.  We consider that to be
    // our "null" value.
    // TODO:  Maybe this would be faster, but violates aliasing rules; does it matter?:
    //    return *reinterpret_cast<const uint64_t*>(this) == 0;
    return (offsetAndKind.get() == 0) & (upper32Bits == 0);
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
      ref->setFar(false, segment->getOffsetTo(ptr));
      ref->farRef.set(segment->getSegmentId());

      // Initialize the landing pad to indicate that the data immediately follows the pad.
      ref = reinterpret_cast<WireReference*>(ptr);
      ref->setKindAndTarget(kind, ptr + REFERENCE_SIZE_IN_WORDS);

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
      WireReference* pad =
          reinterpret_cast<WireReference*>(segment->getPtrUnchecked(ref->farPositionInSegment()));
      if (!ref->isDoubleFar()) {
        ref = pad;
        return pad->target();
      }

      // Landing pad is another far pointer.  It is followed by a tag describing the pointed-to
      // object.
      ref = pad + 1;

      segment = segment->getArena()->getSegment(pad->farRef.segmentId.get());
      return segment->getPtrUnchecked(pad->farPositionInSegment());
    } else {
      return ref->target();
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(
      const word* followFars(const WireReference*& ref, SegmentReader*& segment)) {
    if (ref->kind() == WireReference::FAR) {
      // Look up the segment containing the landing pad.
      segment = segment->getArena()->tryGetSegment(ref->farRef.segmentId.get());
      VALIDATE_INPUT(segment != nullptr, "Message contains far pointer to unknown segment.") {
        return nullptr;
      }

      // Find the landing pad and check that it is within bounds.
      const word* ptr = segment->getStartPtr() + ref->farPositionInSegment();
      WordCount padWords = (1 + ref->isDoubleFar()) * REFERENCE_SIZE_IN_WORDS;
      VALIDATE_INPUT(segment->containsInterval(ptr, ptr + padWords),
                     "Message contains out-of-bounds far pointer.") {
        return nullptr;
      }

      const WireReference* pad = reinterpret_cast<const WireReference*>(ptr);

      // If this is not a double-far then the landing pad is our final pointer.
      if (!ref->isDoubleFar()) {
        ref = pad;
        return pad->target();
      }

      // Landing pad is another far pointer.  It is followed by a tag describing the pointed-to
      // object.
      ref = pad + 1;

      segment = segment->getArena()->tryGetSegment(pad->farRef.segmentId.get());
      VALIDATE_INPUT(segment != nullptr,
                     "Message contains double-far pointer to unknown segment.") {
        return nullptr;
      }

      return segment->getStartPtr() + pad->farPositionInSegment();
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

          dst->structRef.set(src->structRef.dataSize.get(), src->structRef.refCount.get());
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

            CHECK(srcTag->kind() == WireReference::STRUCT,
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
        FAIL_PRECOND("Copy source message contained unexpected kind.");
        break;
    }

    return nullptr;
  }

  // -----------------------------------------------------------------

  static CAPNPROTO_ALWAYS_INLINE(StructBuilder initStructReference(
      WireReference* ref, SegmentBuilder* segment, StructSize size)) {
    // Allocate space for the new struct.  Newly-allocated space is automatically zeroed.
    word* ptr = allocate(ref, segment, size.total(), WireReference::STRUCT);

    // Initialize the reference.
    ref->structRef.set(size);

    // Build the StructBuilder.
    return StructBuilder(segment, ptr, reinterpret_cast<WireReference*>(ptr + size.data));
  }

  static CAPNPROTO_ALWAYS_INLINE(StructBuilder getWritableStructReference(
      WireReference* ref, SegmentBuilder* segment, StructSize size, const word* defaultValue)) {
    word* ptr;

    if (ref->isNull()) {
      if (defaultValue == nullptr) {
        ptr = allocate(ref, segment, size.total(), WireReference::STRUCT);
        ref->structRef.set(size);
      } else {
        ptr = copyMessage(segment, ref, reinterpret_cast<const WireReference*>(defaultValue));
      }
    } else {
      ptr = followFars(ref, segment);

      DPRECOND(ref->kind() == WireReference::STRUCT,
          "Called getStruct{Field,Element}() but existing reference is not a struct.");
      DPRECOND(
          ref->structRef.dataSize.get() == size.data,
          "Trying to update struct with incorrect data size.");
      DPRECOND(
          ref->structRef.refCount.get() == size.pointers,
          "Trying to update struct with incorrect reference count.");
    }

    return StructBuilder(segment, ptr, reinterpret_cast<WireReference*>(ptr + size.data));
  }

  static CAPNPROTO_ALWAYS_INLINE(ListBuilder initListReference(
      WireReference* ref, SegmentBuilder* segment, ElementCount elementCount,
      FieldSize elementSize)) {
    DPRECOND(elementSize != FieldSize::INLINE_COMPOSITE,
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
      StructSize elementSize)) {
    auto wordsPerElement = elementSize.total() / ELEMENTS;

    // Allocate the list, prefixed by a single WireReference.
    WordCount wordCount = elementCount * wordsPerElement;
    word* ptr = allocate(ref, segment, REFERENCE_SIZE_IN_WORDS + wordCount, WireReference::LIST);

    // Initialize the reference.
    // INLINE_COMPOSITE lists replace the element count with the word count.
    ref->listRef.setInlineComposite(wordCount);

    // Initialize the list tag.
    reinterpret_cast<WireReference*>(ptr)->setKindAndInlineCompositeListElementCount(
        WireReference::STRUCT, elementCount);
    reinterpret_cast<WireReference*>(ptr)->structRef.set(elementSize);
    ptr += REFERENCE_SIZE_IN_WORDS;

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

      PRECOND(ref->kind() == WireReference::LIST,
          "Called getList{Field,Element}() but existing reference is not a list.");
    }

    if (ref->listRef.elementSize() == FieldSize::INLINE_COMPOSITE) {
      // Read the tag to get the actual element count.
      WireReference* tag = reinterpret_cast<WireReference*>(ptr);
      PRECOND(tag->kind() == WireReference::STRUCT,
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

      PRECOND(ref->kind() == WireReference::LIST,
          "Called getText{Field,Element}() but existing reference is not a list.");
      PRECOND(ref->listRef.elementSize() == FieldSize::BYTE,
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

      PRECOND(ref->kind() == WireReference::LIST,
          "Called getData{Field,Element}() but existing reference is not a list.");
      PRECOND(ref->listRef.elementSize() == FieldSize::BYTE,
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
      if (defaultValue == nullptr) {
        return StructReader(nullptr, nullptr, nullptr, 0 * WORDS, 0 * REFERENCES, 0 * BITS,
                            std::numeric_limits<int>::max());
      }
      segment = nullptr;
      ref = reinterpret_cast<const WireReference*>(defaultValue);
      ptr = ref->target();
    } else if (segment != nullptr) {
      VALIDATE_INPUT(nestingLimit > 0,
            "Message is too deeply-nested or contains cycles.  See capnproto::ReadOptions.") {
        goto useDefault;
      }

      ptr = followFars(ref, segment);
      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        // Already reported the error.
        goto useDefault;
      }

      VALIDATE_INPUT(ref->kind() == WireReference::STRUCT,
            "Message contains non-struct reference where struct reference was expected.") {
        goto useDefault;
      }

      VALIDATE_INPUT(segment->containsInterval(ptr, ptr + ref->structRef.wordSize()),
            "Message contained out-of-bounds struct reference.") {
        goto useDefault;
      }
    } else {
      // Trusted messages don't contain far pointers.
      ptr = ref->target();
    }

    return StructReader(
        segment, ptr, reinterpret_cast<const WireReference*>(ptr + ref->structRef.dataSize.get()),
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
      VALIDATE_INPUT(nestingLimit > 0,
            "Message is too deeply-nested or contains cycles.  See capnproto::ReadOptions.") {
        goto useDefault;
      }

      ptr = followFars(ref, segment);
      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        // Already reported error.
        goto useDefault;
      }

      VALIDATE_INPUT(ref->kind() == WireReference::LIST,
            "Message contains non-list reference where list reference was expected.") {
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
        VALIDATE_INPUT(segment->containsInterval(ptr - REFERENCE_SIZE_IN_WORDS, ptr + wordCount),
              "Message contains out-of-bounds list reference.") {
          goto useDefault;
        }

        VALIDATE_INPUT(tag->kind() == WireReference::STRUCT,
              "INLINE_COMPOSITE lists of non-STRUCT type are not supported.") {
          goto useDefault;
        }

        size = tag->inlineCompositeListElementCount();
        wordsPerElement = tag->structRef.wordSize() / ELEMENTS;

        VALIDATE_INPUT(size * wordsPerElement <= wordCount,
              "INLINE_COMPOSITE list's elements overrun its word count.") {
          goto useDefault;
        }

        // If a struct list was not expected, then presumably a non-struct list was upgraded to a
        // struct list.  We need to manipulate the pointer to point at the first field of the
        // struct.  Together with the "stepBits", this will allow the struct list to be accessed as
        // if it were a primitive list without branching.

        // Check whether the size is compatible.
        switch (expectedElementSize) {
          case FieldSize::VOID:
            break;

          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES:
            VALIDATE_INPUT(tag->structRef.dataSize.get() > 0 * WORDS,
                  "Expected a primitive list, but got a list of pointer-only structs.") {
              goto useDefault;
            }
            break;

          case FieldSize::REFERENCE:
            // We expected a list of references but got a list of structs.  Assuming the first field
            // in the struct is the reference we were looking for, we want to munge the pointer to
            // point at the first element's reference segment.
            ptr += tag->structRef.dataSize.get();
            VALIDATE_INPUT(tag->structRef.refCount.get() > 0 * REFERENCES,
                  "Expected a pointer list, but got a list of data-only structs.") {
              goto useDefault;
            }
            break;

          case FieldSize::INLINE_COMPOSITE:
            break;
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
          tag->structRef.dataSize.get(), tag->structRef.refCount.get(), nestingLimit - 1);

    } else {
      // The elements of the list are NOT structs.
      decltype(BITS/ELEMENTS) step = bitsPerElement(ref->listRef.elementSize());

      if (segment != nullptr) {
        VALIDATE_INPUT(segment->containsInterval(ptr, ptr +
              roundUpToWords(ElementCount64(ref->listRef.elementCount()) * step)),
              "Message contained out-of-bounds list reference.") {
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
            FAIL_CHECK();
            break;
        }

        return ListReader(segment, ptr, ref->listRef.elementCount(), step,
                          dataSize, referenceCount, nestingLimit - 1);
      } else {
        PRECOND(segment != nullptr, "Trusted message had incompatible list element type.");
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

      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        // Already reported error.
        goto useDefault;
      }

      uint size = ref->listRef.elementCount() / ELEMENTS;

      VALIDATE_INPUT(ref->kind() == WireReference::LIST,
            "Message contains non-list reference where text was expected.") {
        goto useDefault;
      }

      VALIDATE_INPUT(ref->listRef.elementSize() == FieldSize::BYTE,
            "Message contains list reference of non-bytes where text was expected.") {
        goto useDefault;
      }

      VALIDATE_INPUT(segment->containsInterval(ptr, ptr +
            roundUpToWords(ref->listRef.elementCount() * (1 * BYTES / ELEMENTS))),
            "Message contained out-of-bounds text reference.") {
        goto useDefault;
      }

      const char* cptr = reinterpret_cast<const char*>(ptr);
      --size;  // NUL terminator

      VALIDATE_INPUT(cptr[size] == '\0', "Message contains text that is not NUL-terminated.") {
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

      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        // Already reported error.
        goto useDefault;
      }

      uint size = ref->listRef.elementCount() / ELEMENTS;

      VALIDATE_INPUT(ref->kind() == WireReference::LIST,
            "Message contains non-list reference where data was expected.") {
        goto useDefault;
      }

      VALIDATE_INPUT(ref->listRef.elementSize() == FieldSize::BYTE,
            "Message contains list reference of non-bytes where data was expected.") {
        goto useDefault;
      }

      VALIDATE_INPUT(segment->containsInterval(ptr, ptr +
            roundUpToWords(ref->listRef.elementCount() * (1 * BYTES / ELEMENTS))),
            "Message contained out-of-bounds data reference.") {
        goto useDefault;
      }

      return Data::Reader(reinterpret_cast<const char*>(ptr), size);
    }
  }
};

// =======================================================================================

StructBuilder StructBuilder::initRoot(
    SegmentBuilder* segment, word* location, StructSize size) {
  return WireHelpers::initStructReference(
      reinterpret_cast<WireReference*>(location), segment, size);
}

StructBuilder StructBuilder::getRoot(
    SegmentBuilder* segment, word* location, StructSize size) {
  return WireHelpers::getWritableStructReference(
      reinterpret_cast<WireReference*>(location), segment, size, nullptr);
}

StructBuilder StructBuilder::initStructField(
    WireReferenceCount refIndex, StructSize size) const {
  return WireHelpers::initStructReference(references + refIndex, segment, size);
}

StructBuilder StructBuilder::getStructField(
    WireReferenceCount refIndex, StructSize size, const word* defaultValue) const {
  return WireHelpers::getWritableStructReference(
      references + refIndex, segment, size, defaultValue);
}

ListBuilder StructBuilder::initListField(
    WireReferenceCount refIndex, FieldSize elementSize, ElementCount elementCount) const {
  return WireHelpers::initListReference(
      references + refIndex, segment,
      elementCount, elementSize);
}

ListBuilder StructBuilder::initStructListField(
    WireReferenceCount refIndex, ElementCount elementCount, StructSize elementSize) const {
  return WireHelpers::initStructListReference(
      references + refIndex, segment, elementCount, elementSize);
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
  // HACK:  We just give maxed-out data size and reference counts because they are only
  // used for checking for field presence.
  static_assert(sizeof(WireReference::structRef.dataSize) == 2,
      "Has the maximum data size changed?");
  static_assert(sizeof(WireReference::structRef.refCount) == 2,
      "Has the maximum reference count changed?");
  return StructReader(segment, data, references,
      0xffff * WORDS, 0xffff * REFERENCES, 0 * BITS, std::numeric_limits<int>::max());
}

StructReader StructReader::readRootTrusted(const word* location) {
  return WireHelpers::readStructReference(nullptr, reinterpret_cast<const WireReference*>(location),
                                          nullptr, std::numeric_limits<int>::max());
}

StructReader StructReader::readRoot(
    const word* location, SegmentReader* segment, int nestingLimit) {
  VALIDATE_INPUT(segment->containsInterval(location, location + REFERENCE_SIZE_IN_WORDS),
                 "Root location out-of-bounds.") {
    location = nullptr;
  }

  return WireHelpers::readStructReference(segment, reinterpret_cast<const WireReference*>(location),
                                          nullptr, nestingLimit);
}

StructReader StructReader::readEmpty() {
  return StructReader(nullptr, nullptr, nullptr, 0 * WORDS, 0 * REFERENCES, 0 * BITS,
                      std::numeric_limits<int>::max());
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
    WireReferenceCount index, ElementCount elementCount, StructSize elementSize) const {
  return WireHelpers::initStructListReference(
      reinterpret_cast<WireReference*>(ptr) + index, segment,
      elementCount, elementSize);
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
  PRECOND(elementSize != FieldSize::INLINE_COMPOSITE,
      "Need to call the other asReader() overload for INLINE_COMPOSITE lists.");
  return ListReader(segment, ptr, elementCount, bitsPerElement(elementSize),
                    std::numeric_limits<int>::max());
}

ListReader ListBuilder::asReader(WordCount dataSize, WireReferenceCount referenceCount) const {
  return ListReader(segment, ptr, elementCount,
      (dataSize + referenceCount * WORDS_PER_REFERENCE) * BITS_PER_WORD / ELEMENTS,
      dataSize, referenceCount, std::numeric_limits<int>::max());
}

StructReader ListReader::getStructElement(ElementCount index) const {
  VALIDATE_INPUT((segment == nullptr) | (nestingLimit > 0),
        "Message is too deeply-nested or contains cycles.  See capnproto::ReadOptions.") {
    return StructReader::readEmpty();
  }

  BitCount64 indexBit = ElementCount64(index) * stepBits;
  const byte* structPtr = reinterpret_cast<const byte*>(ptr) + indexBit / BITS_PER_BYTE;
  return StructReader(
      segment, structPtr,
      reinterpret_cast<const WireReference*>(structPtr + structDataSize * BYTES_PER_WORD),
      structDataSize, structReferenceCount, indexBit % BITS_PER_BYTE, nestingLimit - 1);
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
