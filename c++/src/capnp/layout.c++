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

#define CAPNP_PRIVATE
#include "layout.h"
#include <kj/debug.h>
#include "arena.h"
#include <string.h>
#include <limits>
#include <stdlib.h>

namespace capnp {
namespace _ {  // private

// =======================================================================================

struct WirePointer {
  // A pointer, in exactly the format in which it appears on the wire.

  // Copying and moving is not allowed because the offset would become wrong.
  WirePointer(const WirePointer& other) = delete;
  WirePointer(WirePointer&& other) = delete;
  WirePointer& operator=(const WirePointer& other) = delete;
  WirePointer& operator=(WirePointer&& other) = delete;

  // -----------------------------------------------------------------
  // Common part of all pointers:  kind + offset
  //
  // Actually this is not terribly common.  The "offset" could actually be different things
  // depending on the context:
  // - For a regular (e.g. struct/list) pointer, a signed word offset from the word immediately
  //   following the pointer pointer.  (The off-by-one means the offset is more often zero, saving
  //   bytes on the wire when packed.)
  // - For an inline composite list tag (not really a pointer, but structured similarly), an
  //   element count.
  // - For a FAR pointer, an unsigned offset into the target segment.
  // - For a FAR landing pad, zero indicates that the target value immediately follows the pad while
  //   1 indicates that the pad is followed by another FAR pointer that actually points at the
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

  KJ_ALWAYS_INLINE(Kind kind() const) {
    return static_cast<Kind>(offsetAndKind.get() & 3);
  }

  KJ_ALWAYS_INLINE(word* target()) {
    return reinterpret_cast<word*>(this) + 1 + (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  KJ_ALWAYS_INLINE(const word* target() const) {
    return reinterpret_cast<const word*>(this) + 1 +
        (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  KJ_ALWAYS_INLINE(void setKindAndTarget(Kind kind, word* target)) {
    offsetAndKind.set(((target - reinterpret_cast<word*>(this) - 1) << 2) | kind);
  }
  KJ_ALWAYS_INLINE(void setKindWithZeroOffset(Kind kind)) {
    offsetAndKind.set(kind);
  }

  KJ_ALWAYS_INLINE(ElementCount inlineCompositeListElementCount() const) {
    return (offsetAndKind.get() >> 2) * ELEMENTS;
  }
  KJ_ALWAYS_INLINE(void setKindAndInlineCompositeListElementCount(
      Kind kind, ElementCount elementCount)) {
    offsetAndKind.set(((elementCount / ELEMENTS) << 2) | kind);
  }

  KJ_ALWAYS_INLINE(WordCount farPositionInSegment() const) {
    KJ_DREQUIRE(kind() == FAR,
        "positionInSegment() should only be called on FAR pointers.");
    return (offsetAndKind.get() >> 3) * WORDS;
  }
  KJ_ALWAYS_INLINE(bool isDoubleFar() const) {
    KJ_DREQUIRE(kind() == FAR,
        "isDoubleFar() should only be called on FAR pointers.");
    return (offsetAndKind.get() >> 2) & 1;
  }
  KJ_ALWAYS_INLINE(void setFar(bool isDoubleFar, WordCount pos)) {
    offsetAndKind.set(((pos / WORDS) << 3) | (static_cast<uint32_t>(isDoubleFar) << 2) |
                      static_cast<uint32_t>(Kind::FAR));
  }

  // -----------------------------------------------------------------
  // Part of pointer that depends on the kind.

  union {
    uint32_t upper32Bits;

    struct {
      WireValue<WordCount16> dataSize;
      WireValue<WirePointerCount16> ptrCount;

      inline WordCount wordSize() const {
        return dataSize.get() + ptrCount.get() * WORDS_PER_POINTER;
      }

      KJ_ALWAYS_INLINE(void set(WordCount ds, WirePointerCount rc)) {
        dataSize.set(ds);
        ptrCount.set(rc);
      }
      KJ_ALWAYS_INLINE(void set(StructSize size)) {
        dataSize.set(size.data);
        ptrCount.set(size.pointers);
      }
    } structRef;
    // Also covers capabilities.

    struct {
      WireValue<uint32_t> elementSizeAndCount;

      KJ_ALWAYS_INLINE(FieldSize elementSize() const) {
        return static_cast<FieldSize>(elementSizeAndCount.get() & 7);
      }
      KJ_ALWAYS_INLINE(ElementCount elementCount() const) {
        return (elementSizeAndCount.get() >> 3) * ELEMENTS;
      }
      KJ_ALWAYS_INLINE(WordCount inlineCompositeWordCount() const) {
        return elementCount() * (1 * WORDS / ELEMENTS);
      }

      KJ_ALWAYS_INLINE(void set(FieldSize es, ElementCount ec)) {
        KJ_DREQUIRE(ec < (1 << 29) * ELEMENTS, "Lists are limited to 2**29 elements.");
        elementSizeAndCount.set(((ec / ELEMENTS) << 3) | static_cast<int>(es));
      }

      KJ_ALWAYS_INLINE(void setInlineComposite(WordCount wc)) {
        KJ_DREQUIRE(wc < (1 << 29) * WORDS, "Inline composite lists are limited to 2**29 words.");
        elementSizeAndCount.set(((wc / WORDS) << 3) |
                                static_cast<int>(FieldSize::INLINE_COMPOSITE));
      }
    } listRef;

    struct {
      WireValue<SegmentId> segmentId;

      KJ_ALWAYS_INLINE(void set(SegmentId si)) {
        segmentId.set(si);
      }
    } farRef;
  };

  KJ_ALWAYS_INLINE(bool isNull() const) {
    // If the upper 32 bits are zero, this is a pointer to an empty struct.  We consider that to be
    // our "null" value.
    // TODO(perf):  Maybe this would be faster, but violates aliasing rules; does it matter?:
    //    return *reinterpret_cast<const uint64_t*>(this) == 0;
    return (offsetAndKind.get() == 0) & (upper32Bits == 0);
  }
};
static_assert(sizeof(WirePointer) == sizeof(word),
    "capnp::WirePointer is not exactly one word.  This will probably break everything.");
static_assert(POINTERS * WORDS_PER_POINTER * BYTES_PER_WORD / BYTES == sizeof(WirePointer),
    "WORDS_PER_POINTER is wrong.");
static_assert(POINTERS * BYTES_PER_POINTER / BYTES == sizeof(WirePointer),
    "BYTES_PER_POINTER is wrong.");
static_assert(POINTERS * BITS_PER_POINTER / BITS_PER_BYTE / BYTES == sizeof(WirePointer),
    "BITS_PER_POINTER is wrong.");

// =======================================================================================

struct WireHelpers {
  static KJ_ALWAYS_INLINE(WordCount roundBytesUpToWords(ByteCount bytes)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    return (bytes + 7 * BYTES) / BYTES_PER_WORD;
  }

  static KJ_ALWAYS_INLINE(ByteCount roundBitsUpToBytes(BitCount bits)) {
    return (bits + 7 * BITS) / BITS_PER_BYTE;
  }

  // The maximum object size is 4GB - 1 byte.  If measured in bits, this would overflow a 32-bit
  // counter, so we need to accept BitCount64.  However, 32 bits is enough for the returned
  // ByteCounts and WordCounts.

  static KJ_ALWAYS_INLINE(WordCount roundBitsUpToWords(BitCount64 bits)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    return (bits + 63 * BITS) / BITS_PER_WORD;
  }

  static KJ_ALWAYS_INLINE(ByteCount roundBitsUpToBytes(BitCount64 bits)) {
    return (bits + 7 * BITS) / BITS_PER_BYTE;
  }

  static KJ_ALWAYS_INLINE(bool boundsCheck(
      SegmentReader* segment, const word* start, const word* end)) {
    // If segment is null, this is an unchecked message, so we don't do bounds checks.
    return segment == nullptr || segment->containsInterval(start, end);
  }

  static KJ_ALWAYS_INLINE(word* allocate(
      WirePointer*& ref, SegmentBuilder*& segment, WordCount amount,
      WirePointer::Kind kind)) {
    if (!ref->isNull()) zeroObject(segment, ref);

    word* ptr = segment->allocate(amount);

    if (ptr == nullptr) {
      // Need to allocate in a new segment.  We'll need to allocate an extra pointer worth of
      // space to act as the landing pad for a far pointer.

      WordCount amountPlusRef = amount + POINTER_SIZE_IN_WORDS;
      segment = segment->getArena()->getSegmentWithAvailable(amountPlusRef);
      ptr = segment->allocate(amountPlusRef);

      // Set up the original pointer to be a far pointer to the new segment.
      ref->setFar(false, segment->getOffsetTo(ptr));
      ref->farRef.set(segment->getSegmentId());

      // Initialize the landing pad to indicate that the data immediately follows the pad.
      ref = reinterpret_cast<WirePointer*>(ptr);
      ref->setKindAndTarget(kind, ptr + POINTER_SIZE_IN_WORDS);

      // Allocated space follows new pointer.
      return ptr + POINTER_SIZE_IN_WORDS;
    } else {
      ref->setKindAndTarget(kind, ptr);
      return ptr;
    }
  }

  static KJ_ALWAYS_INLINE(word* followFars(
      WirePointer*& ref, word* refTarget, SegmentBuilder*& segment)) {
    // If `ref` is a far pointer, follow it.  On return, `ref` will have been updated to point at
    // a WirePointer that contains the type information about the target object, and a pointer to
    // the object contents is returned.  The caller must NOT use `ref->target()` as this may or may
    // not actually return a valid pointer.  `segment` is also updated to point at the segment which
    // actually contains the object.
    //
    // If `ref` is not a far pointer, this simply returns `refTarget`.  Usually, `refTarget` should
    // be the same as `ref->target()`, but may not be in cases where `ref` is only a tag.

    if (ref->kind() == WirePointer::FAR) {
      segment = segment->getArena()->getSegment(ref->farRef.segmentId.get());
      WirePointer* pad =
          reinterpret_cast<WirePointer*>(segment->getPtrUnchecked(ref->farPositionInSegment()));
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
      return refTarget;
    }
  }

  static KJ_ALWAYS_INLINE(
      const word* followFars(const WirePointer*& ref, SegmentReader*& segment)) {
    // Like the other followFars() but operates on readers.  There is no `refTarget` parameter
    // because `ref->target()` is valid for all use cases of this method.

    // If the segment is null, this is an unchecked message, so there are no FAR pointers.
    if (segment != nullptr && ref->kind() == WirePointer::FAR) {
      // Look up the segment containing the landing pad.
      segment = segment->getArena()->tryGetSegment(ref->farRef.segmentId.get());
      KJ_REQUIRE(segment != nullptr, "Message contains far pointer to unknown segment.") {
        return nullptr;
      }

      // Find the landing pad and check that it is within bounds.
      const word* ptr = segment->getStartPtr() + ref->farPositionInSegment();
      WordCount padWords = (1 + ref->isDoubleFar()) * POINTER_SIZE_IN_WORDS;
      KJ_REQUIRE(boundsCheck(segment, ptr, ptr + padWords),
                 "Message contains out-of-bounds far pointer.") {
        return nullptr;
      }

      const WirePointer* pad = reinterpret_cast<const WirePointer*>(ptr);

      // If this is not a double-far then the landing pad is our final pointer.
      if (!ref->isDoubleFar()) {
        ref = pad;
        return pad->target();
      }

      // Landing pad is another far pointer.  It is followed by a tag describing the pointed-to
      // object.
      ref = pad + 1;

      segment = segment->getArena()->tryGetSegment(pad->farRef.segmentId.get());
      KJ_REQUIRE(segment != nullptr, "Message contains double-far pointer to unknown segment.") {
        return nullptr;
      }

      return segment->getStartPtr() + pad->farPositionInSegment();
    } else {
      return ref->target();
    }
  }

  // -----------------------------------------------------------------

  static void zeroObject(SegmentBuilder* segment, WirePointer* ref) {
    // Zero out the pointed-to object.  Use when the pointer is about to be overwritten making the
    // target object no longer reachable.

    switch (ref->kind()) {
      case WirePointer::STRUCT:
        zeroObject(segment, ref, ref->target());
        break;
      case WirePointer::LIST:
        zeroObject(segment, ref, ref->target());
        break;
      case WirePointer::FAR: {
        segment = segment->getArena()->getSegment(ref->farRef.segmentId.get());
        WirePointer* pad =
            reinterpret_cast<WirePointer*>(segment->getPtrUnchecked(ref->farPositionInSegment()));

        if (ref->isDoubleFar()) {
          segment = segment->getArena()->getSegment(pad->farRef.segmentId.get());
          zeroObject(segment, pad + 1, segment->getPtrUnchecked(pad->farPositionInSegment()));
          memset(pad, 0, sizeof(WirePointer) * 2);
        } else {
          zeroObject(segment, pad);
          memset(pad, 0, sizeof(WirePointer));
        }
        break;
      }
      case WirePointer::RESERVED_3:
        KJ_FAIL_ASSERT("Don't know how to handle RESERVED_3.") {
          break;
        }
        break;
    }
  }

  static void zeroObject(SegmentBuilder* segment, WirePointer* tag, word* ptr) {
    switch (tag->kind()) {
      case WirePointer::STRUCT: {
        WirePointer* pointerSection =
            reinterpret_cast<WirePointer*>(ptr + tag->structRef.dataSize.get());
        uint count = tag->structRef.ptrCount.get() / POINTERS;
        for (uint i = 0; i < count; i++) {
          zeroObject(segment, pointerSection + i);
        }
        memset(ptr, 0, tag->structRef.wordSize() * BYTES_PER_WORD / BYTES);
        break;
      }
      case WirePointer::LIST: {
        switch (tag->listRef.elementSize()) {
          case FieldSize::VOID:
            // Nothing.
            break;
          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES:
            memset(ptr, 0,
                roundBitsUpToWords(ElementCount64(tag->listRef.elementCount()) *
                                   dataBitsPerElement(tag->listRef.elementSize()))
                    * BYTES_PER_WORD / BYTES);
            break;
          case FieldSize::POINTER: {
            uint count = tag->listRef.elementCount() / ELEMENTS;
            for (uint i = 0; i < count; i++) {
              zeroObject(segment, reinterpret_cast<WirePointer*>(ptr) + i);
            }
            break;
          }
          case FieldSize::INLINE_COMPOSITE: {
            WirePointer* elementTag = reinterpret_cast<WirePointer*>(ptr);

            KJ_ASSERT(elementTag->kind() == WirePointer::STRUCT,
                  "Don't know how to handle non-STRUCT inline composite.");
            WordCount dataSize = elementTag->structRef.dataSize.get();
            WirePointerCount pointerCount = elementTag->structRef.ptrCount.get();

            word* pos = ptr + POINTER_SIZE_IN_WORDS;
            uint count = elementTag->inlineCompositeListElementCount() / ELEMENTS;
            for (uint i = 0; i < count; i++) {
              pos += dataSize;

              for (uint j = 0; j < pointerCount / POINTERS; j++) {
                zeroObject(segment, reinterpret_cast<WirePointer*>(pos));
                pos += POINTER_SIZE_IN_WORDS;
              }
            }

            memset(ptr, 0, (elementTag->structRef.wordSize() + POINTER_SIZE_IN_WORDS)
                           * BYTES_PER_WORD / BYTES);
            break;
          }
        }
        break;
      }
      case WirePointer::FAR:
        KJ_FAIL_ASSERT("Unexpected FAR pointer.") {
          break;
        }
        break;
      case WirePointer::RESERVED_3:
        KJ_FAIL_ASSERT("Don't know how to handle RESERVED_3.") {
          break;
        }
        break;
    }
  }

  static KJ_ALWAYS_INLINE(
      void zeroPointerAndFars(SegmentBuilder* segment, WirePointer* ref)) {
    // Zero out the pointer itself and, if it is a far pointer, zero the landing pad as well, but
    // do not zero the object body.  Used when upgrading.

    if (ref->kind() == WirePointer::FAR) {
      word* pad = segment->getArena()->getSegment(ref->farRef.segmentId.get())
          ->getPtrUnchecked(ref->farPositionInSegment());
      memset(pad, 0, sizeof(WirePointer) * (1 + ref->isDoubleFar()));
    }
    memset(ref, 0, sizeof(*ref));
  }


  // -----------------------------------------------------------------

  static WordCount64 totalSize(SegmentReader* segment, const WirePointer* ref, uint nestingLimit) {
    // Compute the total size of the object pointed to, not counting far pointer overhead.

    if (ref->isNull()) {
      return 0 * WORDS;
    }

    KJ_REQUIRE(nestingLimit > 0, "Message is too deeply-nested.") {
      return 0 * WORDS;
    }
    --nestingLimit;

    const word* ptr = followFars(ref, segment);

    WordCount64 result = 0 * WORDS;

    switch (ref->kind()) {
      case WirePointer::STRUCT: {
        KJ_REQUIRE(boundsCheck(segment, ptr, ptr + ref->structRef.wordSize()),
                   "Message contained out-of-bounds struct pointer.") {
          return result;
        }
        result += ref->structRef.wordSize();

        const WirePointer* pointerSection =
            reinterpret_cast<const WirePointer*>(ptr + ref->structRef.dataSize.get());
        uint count = ref->structRef.ptrCount.get() / POINTERS;
        for (uint i = 0; i < count; i++) {
          result += totalSize(segment, pointerSection + i, nestingLimit);
        }
        break;
      }
      case WirePointer::LIST: {
        switch (ref->listRef.elementSize()) {
          case FieldSize::VOID:
            // Nothing.
            break;
          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES: {
            WordCount totalWords = roundBitsUpToWords(
                ElementCount64(ref->listRef.elementCount()) *
                dataBitsPerElement(ref->listRef.elementSize()));
            KJ_REQUIRE(boundsCheck(segment, ptr, ptr + totalWords),
                       "Message contained out-of-bounds list pointer.") {
              return result;
            }
            result += totalWords;
            break;
          }
          case FieldSize::POINTER: {
            WirePointerCount count = ref->listRef.elementCount() * (POINTERS / ELEMENTS);

            KJ_REQUIRE(boundsCheck(segment, ptr, ptr + count * WORDS_PER_POINTER),
                       "Message contained out-of-bounds list pointer.") {
              return result;
            }

            result += count * WORDS_PER_POINTER;

            for (uint i = 0; i < count / POINTERS; i++) {
              result += totalSize(segment, reinterpret_cast<const WirePointer*>(ptr) + i,
                                  nestingLimit);
            }
            break;
          }
          case FieldSize::INLINE_COMPOSITE: {
            WordCount wordCount = ref->listRef.inlineCompositeWordCount();
            KJ_REQUIRE(boundsCheck(segment, ptr, ptr + wordCount + POINTER_SIZE_IN_WORDS),
                       "Message contained out-of-bounds list pointer.") {
              return result;
            }

            result += wordCount + POINTER_SIZE_IN_WORDS;

            const WirePointer* elementTag = reinterpret_cast<const WirePointer*>(ptr);
            ElementCount count = elementTag->inlineCompositeListElementCount();

            KJ_REQUIRE(elementTag->kind() == WirePointer::STRUCT,
                       "Don't know how to handle non-STRUCT inline composite.") {
              return result;
            }

            KJ_REQUIRE(elementTag->structRef.wordSize() / ELEMENTS * count <= wordCount,
                       "Struct list pointer's elements overran size.") {
              return result;
            }

            WordCount dataSize = elementTag->structRef.dataSize.get();
            WirePointerCount pointerCount = elementTag->structRef.ptrCount.get();

            const word* pos = ptr + POINTER_SIZE_IN_WORDS;
            for (uint i = 0; i < count / ELEMENTS; i++) {
              pos += dataSize;

              for (uint j = 0; j < pointerCount / POINTERS; j++) {
                result += totalSize(segment, reinterpret_cast<const WirePointer*>(pos),
                                    nestingLimit);
                pos += POINTER_SIZE_IN_WORDS;
              }
            }
            break;
          }
        }
        break;
      }
      case WirePointer::FAR:
        KJ_FAIL_ASSERT("Unexpected FAR pointer.") {
          break;
        }
        break;
      case WirePointer::RESERVED_3:
        KJ_FAIL_REQUIRE("Don't know how to handle RESERVED_3.") {
          return result;
        }
        break;
    }

    return result;
  }

  // -----------------------------------------------------------------
  // Copy from an unchecked message.

  static KJ_ALWAYS_INLINE(
      void copyStruct(SegmentBuilder* segment, word* dst, const word* src,
                      WordCount dataSize, WirePointerCount pointerCount)) {
    memcpy(dst, src, dataSize * BYTES_PER_WORD / BYTES);

    const WirePointer* srcRefs = reinterpret_cast<const WirePointer*>(src + dataSize);
    WirePointer* dstRefs = reinterpret_cast<WirePointer*>(dst + dataSize);

    for (uint i = 0; i < pointerCount / POINTERS; i++) {
      SegmentBuilder* subSegment = segment;
      WirePointer* dstRef = dstRefs + i;
      copyMessage(subSegment, dstRef, srcRefs + i);
    }
  }

  static word* copyMessage(
      SegmentBuilder*& segment, WirePointer*& dst, const WirePointer* src) {
    // Not always-inline because it's recursive.

    switch (src->kind()) {
      case WirePointer::STRUCT: {
        if (src->isNull()) {
          memset(dst, 0, sizeof(WirePointer));
          return nullptr;
        } else {
          const word* srcPtr = src->target();
          word* dstPtr = allocate(dst, segment, src->structRef.wordSize(), WirePointer::STRUCT);

          copyStruct(segment, dstPtr, srcPtr, src->structRef.dataSize.get(),
                     src->structRef.ptrCount.get());

          dst->structRef.set(src->structRef.dataSize.get(), src->structRef.ptrCount.get());
          return dstPtr;
        }
      }
      case WirePointer::LIST: {
        switch (src->listRef.elementSize()) {
          case FieldSize::VOID:
          case FieldSize::BIT:
          case FieldSize::BYTE:
          case FieldSize::TWO_BYTES:
          case FieldSize::FOUR_BYTES:
          case FieldSize::EIGHT_BYTES: {
            WordCount wordCount = roundBitsUpToWords(
                ElementCount64(src->listRef.elementCount()) *
                dataBitsPerElement(src->listRef.elementSize()));
            const word* srcPtr = src->target();
            word* dstPtr = allocate(dst, segment, wordCount, WirePointer::LIST);
            memcpy(dstPtr, srcPtr, wordCount * BYTES_PER_WORD / BYTES);

            dst->listRef.set(src->listRef.elementSize(), src->listRef.elementCount());
            return dstPtr;
          }

          case FieldSize::POINTER: {
            const WirePointer* srcRefs = reinterpret_cast<const WirePointer*>(src->target());
            WirePointer* dstRefs = reinterpret_cast<WirePointer*>(
                allocate(dst, segment, src->listRef.elementCount() *
                    (1 * POINTERS / ELEMENTS) * WORDS_PER_POINTER,
                    WirePointer::LIST));

            uint n = src->listRef.elementCount() / ELEMENTS;
            for (uint i = 0; i < n; i++) {
              SegmentBuilder* subSegment = segment;
              WirePointer* dstRef = dstRefs + i;
              copyMessage(subSegment, dstRef, srcRefs + i);
            }

            dst->listRef.set(FieldSize::POINTER, src->listRef.elementCount());
            return reinterpret_cast<word*>(dstRefs);
          }

          case FieldSize::INLINE_COMPOSITE: {
            const word* srcPtr = src->target();
            word* dstPtr = allocate(dst, segment,
                src->listRef.inlineCompositeWordCount() + POINTER_SIZE_IN_WORDS,
                WirePointer::LIST);

            dst->listRef.setInlineComposite(src->listRef.inlineCompositeWordCount());

            const WirePointer* srcTag = reinterpret_cast<const WirePointer*>(srcPtr);
            memcpy(dstPtr, srcTag, sizeof(WirePointer));

            const word* srcElement = srcPtr + POINTER_SIZE_IN_WORDS;
            word* dstElement = dstPtr + POINTER_SIZE_IN_WORDS;

            KJ_ASSERT(srcTag->kind() == WirePointer::STRUCT,
                "INLINE_COMPOSITE of lists is not yet supported.");

            uint n = srcTag->inlineCompositeListElementCount() / ELEMENTS;
            for (uint i = 0; i < n; i++) {
              copyStruct(segment, dstElement, srcElement,
                  srcTag->structRef.dataSize.get(), srcTag->structRef.ptrCount.get());
              srcElement += srcTag->structRef.wordSize();
              dstElement += srcTag->structRef.wordSize();
            }
            return dstPtr;
          }
        }
        break;
      }
      default:
        KJ_FAIL_REQUIRE("Copy source message contained unexpected kind.");
        break;
    }

    return nullptr;
  }

  static void transferPointer(SegmentBuilder* dstSegment, WirePointer* dst,
                              SegmentBuilder* srcSegment, WirePointer* src) {
    // Make *dst point to the same object as *src.  Both must reside in the same message, but can
    // be in different segments.  Not always-inline because this is rarely used.
    //
    // Caller MUST zero out the source pointer after calling this, to make sure no later code
    // mistakenly thinks the source location still owns the object.  transferPointer() doesn't do
    // this zeroing itself because many callers transfer several pointers in a loop then zero out
    // the whole section.

    KJ_DASSERT(dst->isNull());
    // We expect the caller to ensure the target is already null so won't leak.

    if (src->isNull()) {
      memset(dst, 0, sizeof(WirePointer));
    } else if (src->kind() == WirePointer::FAR) {
      // Far pointers are position-independent, so we can just copy.
      memcpy(dst, src, sizeof(WirePointer));
    } else {
      transferPointer(dstSegment, dst, srcSegment, src, src->target());
    }
  }

  static void transferPointer(SegmentBuilder* dstSegment, WirePointer* dst,
                              SegmentBuilder* srcSegment, const WirePointer* srcTag,
                              word* srcPtr) {
    // Like the other overload, but splits src into a tag and a target.  Particularly useful for
    // OrphanBuilder.

    if (dstSegment == srcSegment) {
      // Same segment, so create a direct pointer.
      dst->setKindAndTarget(srcTag->kind(), srcPtr);

      // We can just copy the upper 32 bits.  (Use memcpy() to comply with aliasing rules.)
      memcpy(&dst->upper32Bits, &srcTag->upper32Bits, sizeof(srcTag->upper32Bits));
    } else {
      // Need to create a far pointer.  Try to allocate it in the same segment as the source, so
      // that it doesn't need to be a double-far.

      WirePointer* landingPad =
          reinterpret_cast<WirePointer*>(srcSegment->allocate(1 * WORDS));
      if (landingPad == nullptr) {
        // Darn, need a double-far.
        SegmentBuilder* farSegment = srcSegment->getArena()->getSegmentWithAvailable(2 * WORDS);
        landingPad = reinterpret_cast<WirePointer*>(farSegment->allocate(2 * WORDS));
        KJ_DASSERT(landingPad != nullptr,
            "getSegmentWithAvailable() returned segment without space available.");

        landingPad[0].setFar(false, srcSegment->getOffsetTo(srcPtr));
        landingPad[0].farRef.segmentId.set(srcSegment->getSegmentId());

        landingPad[1].setKindWithZeroOffset(srcTag->kind());
        memcpy(&landingPad[1].upper32Bits, &srcTag->upper32Bits, sizeof(srcTag->upper32Bits));

        dst->setFar(true, farSegment->getOffsetTo(reinterpret_cast<word*>(landingPad)));
        dst->farRef.set(farSegment->getSegmentId());
      } else {
        // Simple landing pad is just a pointer.
        landingPad->setKindAndTarget(srcTag->kind(), srcPtr);
        memcpy(&landingPad->upper32Bits, &srcTag->upper32Bits, sizeof(srcTag->upper32Bits));

        dst->setFar(false, srcSegment->getOffsetTo(reinterpret_cast<word*>(landingPad)));
        dst->farRef.set(srcSegment->getSegmentId());
      }
    }
  }

  // -----------------------------------------------------------------

  static KJ_ALWAYS_INLINE(StructBuilder initStructPointer(
      WirePointer* ref, SegmentBuilder* segment, StructSize size)) {
    // Allocate space for the new struct.  Newly-allocated space is automatically zeroed.
    word* ptr = allocate(ref, segment, size.total(), WirePointer::STRUCT);

    // Initialize the pointer.
    ref->structRef.set(size);

    // Build the StructBuilder.
    return StructBuilder(segment, ptr, reinterpret_cast<WirePointer*>(ptr + size.data),
                         size.data * BITS_PER_WORD, size.pointers, 0 * BITS);
  }

  static KJ_ALWAYS_INLINE(StructBuilder getWritableStructPointer(
      WirePointer* ref, SegmentBuilder* segment, StructSize size, const word* defaultValue)) {
    return getWritableStructPointer(ref, ref->target(), segment, size, defaultValue);
  }

  static KJ_ALWAYS_INLINE(StructBuilder getWritableStructPointer(
      WirePointer* ref, word* refTarget, SegmentBuilder* segment, StructSize size,
      const word* defaultValue)) {
    if (ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return initStructPointer(ref, segment, size);
      }
      refTarget = copyMessage(segment, ref, reinterpret_cast<const WirePointer*>(defaultValue));
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    WirePointer* oldRef = ref;
    SegmentBuilder* oldSegment = segment;
    word* oldPtr = followFars(oldRef, refTarget, oldSegment);

    KJ_REQUIRE(oldRef->kind() == WirePointer::STRUCT,
        "Message contains non-struct pointer where struct pointer was expected.") {
      goto useDefault;
    }

    WordCount oldDataSize = oldRef->structRef.dataSize.get();
    WirePointerCount oldPointerCount = oldRef->structRef.ptrCount.get();
    WirePointer* oldPointerSection =
        reinterpret_cast<WirePointer*>(oldPtr + oldDataSize);

    if (oldDataSize < size.data || oldPointerCount < size.pointers) {
      // The space allocated for this struct is too small.  Unlike with readers, we can't just
      // run with it and do bounds checks at access time, because how would we handle writes?
      // Instead, we have to copy the struct to a new space now.

      WordCount newDataSize = std::max<WordCount>(oldDataSize, size.data);
      WirePointerCount newPointerCount =
          std::max<WirePointerCount>(oldPointerCount, size.pointers);
      WordCount totalSize = newDataSize + newPointerCount * WORDS_PER_POINTER;

      // Don't let allocate() zero out the object just yet.
      zeroPointerAndFars(segment, ref);

      word* ptr = allocate(ref, segment, totalSize, WirePointer::STRUCT);
      ref->structRef.set(newDataSize, newPointerCount);

      // Copy data section.
      memcpy(ptr, oldPtr, oldDataSize * BYTES_PER_WORD / BYTES);

      // Copy pointer section.
      WirePointer* newPointerSection = reinterpret_cast<WirePointer*>(ptr + newDataSize);
      for (uint i = 0; i < oldPointerCount / POINTERS; i++) {
        transferPointer(segment, newPointerSection + i, oldSegment, oldPointerSection + i);
      }

      // Zero out old location.  This has two purposes:
      // 1) We don't want to leak the original contents of the struct when the message is written
      //    out as it may contain secrets that the caller intends to remove from the new copy.
      // 2) Zeros will be deflated by packing, making this dead memory almost-free if it ever
      //    hits the wire.
      memset(oldPtr, 0,
             (oldDataSize + oldPointerCount * WORDS_PER_POINTER) * BYTES_PER_WORD / BYTES);

      return StructBuilder(segment, ptr, newPointerSection, newDataSize * BITS_PER_WORD,
                           newPointerCount, 0 * BITS);
    } else {
      return StructBuilder(oldSegment, oldPtr, oldPointerSection, oldDataSize * BITS_PER_WORD,
                           oldPointerCount, 0 * BITS);
    }
  }

  static KJ_ALWAYS_INLINE(ListBuilder initListPointer(
      WirePointer* ref, SegmentBuilder* segment, ElementCount elementCount,
      FieldSize elementSize)) {
    KJ_DREQUIRE(elementSize != FieldSize::INLINE_COMPOSITE,
        "Should have called initStructListPointer() instead.");

    BitCount dataSize = dataBitsPerElement(elementSize) * ELEMENTS;
    WirePointerCount pointerCount = pointersPerElement(elementSize) * ELEMENTS;
    auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;

    // Calculate size of the list.
    WordCount wordCount = roundBitsUpToWords(ElementCount64(elementCount) * step);

    // Allocate the list.
    word* ptr = allocate(ref, segment, wordCount, WirePointer::LIST);

    // Initialize the pointer.
    ref->listRef.set(elementSize, elementCount);

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, step, elementCount, dataSize, pointerCount);
  }

  static KJ_ALWAYS_INLINE(ListBuilder initStructListPointer(
      WirePointer* ref, SegmentBuilder* segment, ElementCount elementCount,
      StructSize elementSize)) {
    if (elementSize.preferredListEncoding != FieldSize::INLINE_COMPOSITE) {
      // Small data-only struct.  Allocate a list of primitives instead.
      return initListPointer(ref, segment, elementCount, elementSize.preferredListEncoding);
    }

    auto wordsPerElement = elementSize.total() / ELEMENTS;

    // Allocate the list, prefixed by a single WirePointer.
    WordCount wordCount = elementCount * wordsPerElement;
    word* ptr = allocate(ref, segment, POINTER_SIZE_IN_WORDS + wordCount, WirePointer::LIST);

    // Initialize the pointer.
    // INLINE_COMPOSITE lists replace the element count with the word count.
    ref->listRef.setInlineComposite(wordCount);

    // Initialize the list tag.
    reinterpret_cast<WirePointer*>(ptr)->setKindAndInlineCompositeListElementCount(
        WirePointer::STRUCT, elementCount);
    reinterpret_cast<WirePointer*>(ptr)->structRef.set(elementSize);
    ptr += POINTER_SIZE_IN_WORDS;

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, wordsPerElement * BITS_PER_WORD, elementCount,
                       elementSize.data * BITS_PER_WORD, elementSize.pointers);
  }

  static KJ_ALWAYS_INLINE(ListBuilder getWritableListPointer(
      WirePointer* origRef, SegmentBuilder* origSegment, FieldSize elementSize,
      const word* defaultValue)) {
    return getWritableListPointer(origRef, origRef->target(), origSegment, elementSize,
                                  defaultValue);
  }

  static KJ_ALWAYS_INLINE(ListBuilder getWritableListPointer(
      WirePointer* origRef, word* origRefTarget, SegmentBuilder* origSegment, FieldSize elementSize,
      const word* defaultValue)) {
    KJ_DREQUIRE(elementSize != FieldSize::INLINE_COMPOSITE,
             "Use getStructList{Element,Field}() for structs.");

    if (origRef->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return ListBuilder();
      }
      origRefTarget = copyMessage(
          origSegment, origRef, reinterpret_cast<const WirePointer*>(defaultValue));
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    // We must verify that the pointer has the right size.  Unlike in
    // getWritableStructListReference(), we never need to "upgrade" the data, because this
    // method is called only for non-struct lists, and there is no allowed upgrade path *to*
    // a non-struct list, only *from* them.

    WirePointer* ref = origRef;
    SegmentBuilder* segment = origSegment;
    word* ptr = followFars(ref, origRefTarget, segment);

    KJ_REQUIRE(ref->kind() == WirePointer::LIST,
        "Called getList{Field,Element}() but existing pointer is not a list.") {
      goto useDefault;
    }

    FieldSize oldSize = ref->listRef.elementSize();

    if (oldSize == FieldSize::INLINE_COMPOSITE) {
      // The existing element size is INLINE_COMPOSITE, which means that it is at least two
      // words, which makes it bigger than the expected element size.  Since fields can only
      // grow when upgraded, the existing data must have been written with a newer version of
      // the protocol.  We therefore never need to upgrade the data in this case, but we do
      // need to validate that it is a valid upgrade from what we expected.

      // Read the tag to get the actual element count.
      WirePointer* tag = reinterpret_cast<WirePointer*>(ptr);
      KJ_REQUIRE(tag->kind() == WirePointer::STRUCT,
          "INLINE_COMPOSITE list with non-STRUCT elements not supported.");
      ptr += POINTER_SIZE_IN_WORDS;

      WordCount dataSize = tag->structRef.dataSize.get();
      WirePointerCount pointerCount = tag->structRef.ptrCount.get();

      switch (elementSize) {
        case FieldSize::VOID:
          // Anything is a valid upgrade from Void.
          break;

        case FieldSize::BIT:
        case FieldSize::BYTE:
        case FieldSize::TWO_BYTES:
        case FieldSize::FOUR_BYTES:
        case FieldSize::EIGHT_BYTES:
          KJ_REQUIRE(dataSize >= 1 * WORDS,
                     "Existing list value is incompatible with expected type.") {
            goto useDefault;
          }
          break;

        case FieldSize::POINTER:
          KJ_REQUIRE(pointerCount >= 1 * POINTERS,
                     "Existing list value is incompatible with expected type.") {
            goto useDefault;
          }
          // Adjust the pointer to point at the reference segment.
          ptr += dataSize;
          break;

        case FieldSize::INLINE_COMPOSITE:
          KJ_FAIL_ASSERT("Can't get here.");
          break;
      }

      // OK, looks valid.

      return ListBuilder(segment, ptr,
                         tag->structRef.wordSize() * BITS_PER_WORD / ELEMENTS,
                         tag->inlineCompositeListElementCount(),
                         dataSize * BITS_PER_WORD, pointerCount);
    } else {
      BitCount dataSize = dataBitsPerElement(oldSize) * ELEMENTS;
      WirePointerCount pointerCount = pointersPerElement(oldSize) * ELEMENTS;

      KJ_REQUIRE(dataSize >= dataBitsPerElement(elementSize) * ELEMENTS,
                 "Existing list value is incompatible with expected type.") {
        goto useDefault;
      }
      KJ_REQUIRE(pointerCount >= pointersPerElement(elementSize) * ELEMENTS,
                 "Existing list value is incompatible with expected type.") {
        goto useDefault;
      }

      auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;
      return ListBuilder(segment, ptr, step, ref->listRef.elementCount(),
                         dataSize, pointerCount);
    }
  }

  static KJ_ALWAYS_INLINE(ListBuilder getWritableStructListPointer(
      WirePointer* origRef, SegmentBuilder* origSegment, StructSize elementSize,
      const word* defaultValue)) {
    return getWritableStructListPointer(origRef, origRef->target(), origSegment, elementSize,
                                        defaultValue);
  }
  static KJ_ALWAYS_INLINE(ListBuilder getWritableStructListPointer(
      WirePointer* origRef, word* origRefTarget, SegmentBuilder* origSegment,
      StructSize elementSize, const word* defaultValue)) {
    if (origRef->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return ListBuilder();
      }
      origRefTarget = copyMessage(
          origSegment, origRef, reinterpret_cast<const WirePointer*>(defaultValue));
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    // We must verify that the pointer has the right size and potentially upgrade it if not.

    WirePointer* oldRef = origRef;
    SegmentBuilder* oldSegment = origSegment;
    word* oldPtr = followFars(oldRef, origRefTarget, oldSegment);

    KJ_REQUIRE(oldRef->kind() == WirePointer::LIST,
               "Called getList{Field,Element}() but existing pointer is not a list.") {
      goto useDefault;
    }

    FieldSize oldSize = oldRef->listRef.elementSize();

    if (oldSize == FieldSize::INLINE_COMPOSITE) {
      // Existing list is INLINE_COMPOSITE, but we need to verify that the sizes match.

      WirePointer* oldTag = reinterpret_cast<WirePointer*>(oldPtr);
      oldPtr += POINTER_SIZE_IN_WORDS;
      KJ_REQUIRE(oldTag->kind() == WirePointer::STRUCT,
                 "INLINE_COMPOSITE list with non-STRUCT elements not supported.") {
        goto useDefault;
      }

      WordCount oldDataSize = oldTag->structRef.dataSize.get();
      WirePointerCount oldPointerCount = oldTag->structRef.ptrCount.get();
      auto oldStep = (oldDataSize + oldPointerCount * WORDS_PER_POINTER) / ELEMENTS;
      ElementCount elementCount = oldTag->inlineCompositeListElementCount();

      if (oldDataSize >= elementSize.data && oldPointerCount >= elementSize.pointers) {
        // Old size is at least as large as we need.  Ship it.
        return ListBuilder(oldSegment, oldPtr, oldStep * BITS_PER_WORD, elementCount,
                           oldDataSize * BITS_PER_WORD, oldPointerCount);
      }

      // The structs in this list are smaller than expected, probably written using an older
      // version of the protocol.  We need to make a copy and expand them.

      WordCount newDataSize = std::max<WordCount>(oldDataSize, elementSize.data);
      WirePointerCount newPointerCount =
          std::max<WirePointerCount>(oldPointerCount, elementSize.pointers);
      auto newStep = (newDataSize + newPointerCount * WORDS_PER_POINTER) / ELEMENTS;
      WordCount totalSize = newStep * elementCount;

      // Don't let allocate() zero out the object just yet.
      zeroPointerAndFars(origSegment, origRef);

      word* newPtr = allocate(origRef, origSegment, totalSize + POINTER_SIZE_IN_WORDS,
                              WirePointer::LIST);
      origRef->listRef.setInlineComposite(totalSize);

      WirePointer* newTag = reinterpret_cast<WirePointer*>(newPtr);
      newTag->setKindAndInlineCompositeListElementCount(WirePointer::STRUCT, elementCount);
      newTag->structRef.set(newDataSize, newPointerCount);
      newPtr += POINTER_SIZE_IN_WORDS;

      word* src = oldPtr;
      word* dst = newPtr;
      for (uint i = 0; i < elementCount / ELEMENTS; i++) {
        // Copy data section.
        memcpy(dst, src, oldDataSize * BYTES_PER_WORD / BYTES);

        // Copy pointer section.
        WirePointer* newPointerSection = reinterpret_cast<WirePointer*>(dst + newDataSize);
        WirePointer* oldPointerSection = reinterpret_cast<WirePointer*>(src + oldDataSize);
        for (uint i = 0; i < oldPointerCount / POINTERS; i++) {
          transferPointer(origSegment, newPointerSection + i, oldSegment, oldPointerSection + i);
        }

        dst += newStep * (1 * ELEMENTS);
        src += oldStep * (1 * ELEMENTS);
      }

      // Zero out old location.  See explanation in getWritableStructPointer().
      memset(oldPtr, 0, oldStep * elementCount * BYTES_PER_WORD / BYTES);

      return ListBuilder(origSegment, newPtr, newStep * BITS_PER_WORD, elementCount,
                         newDataSize * BITS_PER_WORD, newPointerCount);
    } else if (oldSize == elementSize.preferredListEncoding) {
      // Old size matches exactly.

      auto dataSize = dataBitsPerElement(oldSize);
      auto pointerCount = pointersPerElement(oldSize);
      auto step = dataSize + pointerCount * BITS_PER_POINTER;

      return ListBuilder(oldSegment, oldPtr, step, oldRef->listRef.elementCount(),
                         dataSize * (1 * ELEMENTS), pointerCount * (1 * ELEMENTS));
    } else {
      switch (elementSize.preferredListEncoding) {
        case FieldSize::VOID:
          // No expectations.
          break;
        case FieldSize::POINTER:
          KJ_REQUIRE(oldSize == FieldSize::POINTER || oldSize == FieldSize::VOID,
                     "Struct list has incompatible element size.") {
            goto useDefault;
          }
          break;
        case FieldSize::INLINE_COMPOSITE:
          // Old size can be anything.
          break;
        case FieldSize::BIT:
        case FieldSize::BYTE:
        case FieldSize::TWO_BYTES:
        case FieldSize::FOUR_BYTES:
        case FieldSize::EIGHT_BYTES:
          // Preferred size is data-only.
          KJ_REQUIRE(oldSize != FieldSize::POINTER,
                     "Struct list has incompatible element size.") {
            goto useDefault;
          }
          break;
      }

      // OK, the old size is compatible with the preferred, but is not exactly the same.  We may
      // need to upgrade it.

      BitCount oldDataSize = dataBitsPerElement(oldSize) * ELEMENTS;
      WirePointerCount oldPointerCount = pointersPerElement(oldSize) * ELEMENTS;
      auto oldStep = (oldDataSize + oldPointerCount * BITS_PER_POINTER) / ELEMENTS;
      ElementCount elementCount = oldRef->listRef.elementCount();

      if (oldSize >= elementSize.preferredListEncoding) {
        // The old size is at least as large as the preferred, so we don't need to upgrade.
        return ListBuilder(oldSegment, oldPtr, oldStep, elementCount,
                           oldDataSize, oldPointerCount);
      }

      // Upgrade is necessary.

      if (oldSize == FieldSize::VOID) {
        // Nothing to copy, just allocate a new list.
        return initStructListPointer(origRef, origSegment, elementCount, elementSize);
      } else if (elementSize.preferredListEncoding == FieldSize::INLINE_COMPOSITE) {
        // Upgrading to an inline composite list.

        WordCount newDataSize = elementSize.data;
        WirePointerCount newPointerCount = elementSize.pointers;

        if (oldSize == FieldSize::POINTER) {
          newPointerCount = std::max(newPointerCount, 1 * POINTERS);
        } else {
          // Old list contains data elements, so we need at least 1 word of data.
          newDataSize = std::max(newDataSize, 1 * WORDS);
        }

        auto newStep = (newDataSize + newPointerCount * WORDS_PER_POINTER) / ELEMENTS;
        WordCount totalWords = elementCount * newStep;

        // Don't let allocate() zero out the object just yet.
        zeroPointerAndFars(origSegment, origRef);

        word* newPtr = allocate(origRef, origSegment, totalWords + POINTER_SIZE_IN_WORDS,
                                WirePointer::LIST);
        origRef->listRef.setInlineComposite(totalWords);

        WirePointer* tag = reinterpret_cast<WirePointer*>(newPtr);
        tag->setKindAndInlineCompositeListElementCount(WirePointer::STRUCT, elementCount);
        tag->structRef.set(newDataSize, newPointerCount);
        newPtr += POINTER_SIZE_IN_WORDS;

        if (oldSize == FieldSize::POINTER) {
          WirePointer* dst = reinterpret_cast<WirePointer*>(newPtr + newDataSize);
          WirePointer* src = reinterpret_cast<WirePointer*>(oldPtr);
          for (uint i = 0; i < elementCount / ELEMENTS; i++) {
            transferPointer(origSegment, dst, oldSegment, src);
            dst += newStep / WORDS_PER_POINTER * (1 * ELEMENTS);
            ++src;
          }
        } else if (oldSize == FieldSize::BIT) {
          word* dst = newPtr;
          char* src = reinterpret_cast<char*>(oldPtr);
          for (uint i = 0; i < elementCount / ELEMENTS; i++) {
            *reinterpret_cast<char*>(dst) = (src[i/8] >> (i%8)) & 1;
            dst += newStep * (1 * ELEMENTS);
          }
        } else {
          word* dst = newPtr;
          char* src = reinterpret_cast<char*>(oldPtr);
          ByteCount oldByteStep = oldDataSize / BITS_PER_BYTE;
          for (uint i = 0; i < elementCount / ELEMENTS; i++) {
            memcpy(dst, src, oldByteStep / BYTES);
            src += oldByteStep / BYTES;
            dst += newStep * (1 * ELEMENTS);
          }
        }

        // Zero out old location.  See explanation in getWritableStructPointer().
        memset(oldPtr, 0, roundBitsUpToBytes(oldStep * elementCount) / BYTES);

        return ListBuilder(origSegment, newPtr, newStep * BITS_PER_WORD, elementCount,
                           newDataSize * BITS_PER_WORD, newPointerCount);

      } else {
        // If oldSize were POINTER or EIGHT_BYTES then the preferred size must be
        // INLINE_COMPOSITE because any other compatible size would not require an upgrade.
        KJ_ASSERT(oldSize < FieldSize::EIGHT_BYTES);

        // If the preferred size were BIT then oldSize must be VOID, but we handled that case
        // above.
        KJ_ASSERT(elementSize.preferredListEncoding >= FieldSize::BIT);

        // OK, so the expected list elements are all data and between 1 byte and 1 word each,
        // and the old element are data between 1 bit and 4 bytes.  We're upgrading from one
        // primitive data type to another, larger one.

        BitCount newDataSize =
            dataBitsPerElement(elementSize.preferredListEncoding) * ELEMENTS;

        WordCount totalWords =
            roundBitsUpToWords(BitCount64(newDataSize) * (elementCount / ELEMENTS));

        // Don't let allocate() zero out the object just yet.
        zeroPointerAndFars(origSegment, origRef);

        word* newPtr = allocate(origRef, origSegment, totalWords, WirePointer::LIST);
        origRef->listRef.set(elementSize.preferredListEncoding, elementCount);

        char* newBytePtr = reinterpret_cast<char*>(newPtr);
        char* oldBytePtr = reinterpret_cast<char*>(oldPtr);
        ByteCount newDataByteSize = newDataSize / BITS_PER_BYTE;
        if (oldSize == FieldSize::BIT) {
          for (uint i = 0; i < elementCount / ELEMENTS; i++) {
            *newBytePtr = (oldBytePtr[i/8] >> (i%8)) & 1;
            newBytePtr += newDataByteSize / BYTES;
          }
        } else {
          ByteCount oldDataByteSize = oldDataSize / BITS_PER_BYTE;
          for (uint i = 0; i < elementCount / ELEMENTS; i++) {
            memcpy(newBytePtr, oldBytePtr, oldDataByteSize / BYTES);
            oldBytePtr += oldDataByteSize / BYTES;
            newBytePtr += newDataByteSize / BYTES;
          }
        }

        // Zero out old location.  See explanation in getWritableStructPointer().
        memset(oldPtr, 0, roundBitsUpToBytes(oldStep * elementCount) / BYTES);

        return ListBuilder(origSegment, newPtr, newDataSize / ELEMENTS, elementCount,
                           newDataSize, 0 * POINTERS);
      }
    }
  }

  static KJ_ALWAYS_INLINE(Text::Builder initTextPointer(
      WirePointer* ref, SegmentBuilder* segment, ByteCount size)) {
    // The byte list must include a NUL terminator.
    ByteCount byteSize = size + 1 * BYTES;

    // Allocate the space.
    word* ptr = allocate(ref, segment, roundBytesUpToWords(byteSize), WirePointer::LIST);

    // Initialize the pointer.
    ref->listRef.set(FieldSize::BYTE, byteSize * (1 * ELEMENTS / BYTES));

    // Build the Text::Builder.  This will initialize the NUL terminator.
    return Text::Builder(reinterpret_cast<char*>(ptr), size / BYTES);
  }

  static KJ_ALWAYS_INLINE(void setTextPointer(
      WirePointer* ref, SegmentBuilder* segment, Text::Reader value)) {
    memcpy(initTextPointer(ref, segment, value.size() * BYTES).begin(),
           value.begin(), value.size());
  }

  static KJ_ALWAYS_INLINE(Text::Builder getWritableTextPointer(
      WirePointer* ref, SegmentBuilder* segment,
      const void* defaultValue, ByteCount defaultSize)) {
    return getWritableTextPointer(ref, ref->target(), segment, defaultValue, defaultSize);
  }

  static KJ_ALWAYS_INLINE(Text::Builder getWritableTextPointer(
      WirePointer* ref, word* refTarget, SegmentBuilder* segment,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref->isNull()) {
      if (defaultSize == 0 * BYTES) {
        return nullptr;
      } else {
        Text::Builder builder = initTextPointer(ref, segment, defaultSize);
        memcpy(builder.begin(), defaultValue, defaultSize / BYTES);
        return builder;
      }
    } else {
      word* ptr = followFars(ref, refTarget, segment);

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
          "Called getText{Field,Element}() but existing pointer is not a list.");
      KJ_REQUIRE(ref->listRef.elementSize() == FieldSize::BYTE,
          "Called getText{Field,Element}() but existing list pointer is not byte-sized.");

      // Subtract 1 from the size for the NUL terminator.
      return Text::Builder(reinterpret_cast<char*>(ptr), ref->listRef.elementCount() / ELEMENTS - 1);
    }
  }

  static KJ_ALWAYS_INLINE(Data::Builder initDataPointer(
      WirePointer* ref, SegmentBuilder* segment, ByteCount size)) {
    // Allocate the space.
    word* ptr = allocate(ref, segment, roundBytesUpToWords(size), WirePointer::LIST);

    // Initialize the pointer.
    ref->listRef.set(FieldSize::BYTE, size * (1 * ELEMENTS / BYTES));

    // Build the Data::Builder.
    return Data::Builder(reinterpret_cast<byte*>(ptr), size / BYTES);
  }

  static KJ_ALWAYS_INLINE(void setDataPointer(
      WirePointer* ref, SegmentBuilder* segment, Data::Reader value)) {
    memcpy(initDataPointer(ref, segment, value.size() * BYTES).begin(),
           value.begin(), value.size());
  }

  static KJ_ALWAYS_INLINE(Data::Builder getWritableDataPointer(
      WirePointer* ref, SegmentBuilder* segment,
      const void* defaultValue, ByteCount defaultSize)) {
    return getWritableDataPointer(ref, ref->target(), segment, defaultValue, defaultSize);
  }

  static KJ_ALWAYS_INLINE(Data::Builder getWritableDataPointer(
      WirePointer* ref, word* refTarget, SegmentBuilder* segment,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref->isNull()) {
      if (defaultSize == 0 * BYTES) {
        return nullptr;
      } else {
        Data::Builder builder = initDataPointer(ref, segment, defaultSize);
        memcpy(builder.begin(), defaultValue, defaultSize / BYTES);
        return builder;
      }
    } else {
      word* ptr = followFars(ref, refTarget, segment);

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
          "Called getData{Field,Element}() but existing pointer is not a list.");
      KJ_REQUIRE(ref->listRef.elementSize() == FieldSize::BYTE,
          "Called getData{Field,Element}() but existing list pointer is not byte-sized.");

      return Data::Builder(reinterpret_cast<byte*>(ptr), ref->listRef.elementCount() / ELEMENTS);
    }
  }

  static KJ_ALWAYS_INLINE(ObjectBuilder getWritableObjectPointer(
      SegmentBuilder* segment, WirePointer* ref, const word* defaultValue)) {
    return getWritableObjectPointer(segment, ref, ref->target(), defaultValue);
  }

  static KJ_ALWAYS_INLINE(ObjectBuilder getWritableObjectPointer(
      SegmentBuilder* segment, WirePointer* ref, word* refTarget, const word* defaultValue)) {
    word* ptr;

    if (ref->isNull()) {
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return ObjectBuilder();
      } else {
        ptr = copyMessage(segment, ref, reinterpret_cast<const WirePointer*>(defaultValue));
      }
    } else {
      ptr = followFars(ref, refTarget, segment);
    }

    if (ref->kind() == WirePointer::LIST) {
      if (ref->listRef.elementSize() == FieldSize::INLINE_COMPOSITE) {
        // Read the tag to get the actual element count.
        WirePointer* tag = reinterpret_cast<WirePointer*>(ptr);
        KJ_REQUIRE(tag->kind() == WirePointer::STRUCT,
            "INLINE_COMPOSITE list with non-STRUCT elements not supported.");

        // First list element is at tag + 1 pointer.
        return ObjectBuilder(
            ListBuilder(segment, tag + 1, tag->structRef.wordSize() * BITS_PER_WORD / ELEMENTS,
                        tag->inlineCompositeListElementCount(),
                        tag->structRef.dataSize.get() * BITS_PER_WORD,
                        tag->structRef.ptrCount.get()));
      } else {
        BitCount dataSize = dataBitsPerElement(ref->listRef.elementSize()) * ELEMENTS;
        WirePointerCount pointerCount =
            pointersPerElement(ref->listRef.elementSize()) * ELEMENTS;
        auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;
        return ObjectBuilder(ListBuilder(
            segment, ptr, step, ref->listRef.elementCount(), dataSize, pointerCount));
      }
    } else {
      return ObjectBuilder(StructBuilder(
          segment, ptr,
          reinterpret_cast<WirePointer*>(ptr + ref->structRef.dataSize.get()),
          ref->structRef.dataSize.get() * BITS_PER_WORD,
          ref->structRef.ptrCount.get(),
          0 * BITS));
    }
  }

  static word* setStructPointer(SegmentBuilder* segment, WirePointer* ref, StructReader value) {
    WordCount dataSize = roundBitsUpToWords(value.dataSize);
    WordCount totalSize = dataSize + value.pointerCount * WORDS_PER_POINTER;

    word* ptr = allocate(ref, segment, totalSize, WirePointer::STRUCT);
    ref->structRef.set(dataSize, value.pointerCount);

    if (value.dataSize == 1 * BITS) {
      *reinterpret_cast<char*>(ptr) = value.getDataField<bool>(0 * ELEMENTS);
    } else {
      memcpy(ptr, value.data, value.dataSize / BITS_PER_BYTE / BYTES);
    }

    WirePointer* pointerSection = reinterpret_cast<WirePointer*>(ptr + dataSize);
    for (uint i = 0; i < value.pointerCount / POINTERS; i++) {
      setObjectPointer(segment, pointerSection + i, readObjectPointer(
          value.segment, value.pointers + i, nullptr, value.nestingLimit));
    }

    return ptr;
  }

  static word* setListPointer(SegmentBuilder* segment, WirePointer* ref, ListReader value) {
    WordCount totalSize = roundBitsUpToWords(value.elementCount * value.step);

    if (value.step * ELEMENTS <= BITS_PER_WORD * WORDS) {
      // List of non-structs.
      word* ptr = allocate(ref, segment, totalSize, WirePointer::LIST);

      if (value.structPointerCount == 1 * POINTERS) {
        // List of pointers.
        ref->listRef.set(FieldSize::POINTER, value.elementCount);
        for (uint i = 0; i < value.elementCount / ELEMENTS; i++) {
          setObjectPointer(segment, reinterpret_cast<WirePointer*>(ptr) + i, readObjectPointer(
              value.segment, reinterpret_cast<const WirePointer*>(value.ptr) + i,
              nullptr, value.nestingLimit));
        }
      } else {
        // List of data.
        FieldSize elementSize = FieldSize::VOID;
        switch (value.step * ELEMENTS / BITS) {
          case 0: elementSize = FieldSize::VOID; break;
          case 1: elementSize = FieldSize::BIT; break;
          case 8: elementSize = FieldSize::BYTE; break;
          case 16: elementSize = FieldSize::TWO_BYTES; break;
          case 32: elementSize = FieldSize::FOUR_BYTES; break;
          case 64: elementSize = FieldSize::EIGHT_BYTES; break;
          default:
            KJ_FAIL_ASSERT("invalid list step size", value.step * ELEMENTS / BITS);
            break;
        }

        ref->listRef.set(elementSize, value.elementCount);
        memcpy(ptr, value.ptr, totalSize * BYTES_PER_WORD / BYTES);
      }

      return ptr;
    } else {
      // List of structs.
      word* ptr = allocate(ref, segment, totalSize + POINTER_SIZE_IN_WORDS, WirePointer::LIST);
      ref->listRef.setInlineComposite(totalSize);

      WordCount dataSize = roundBitsUpToWords(value.structDataSize);
      WirePointerCount pointerCount = value.structPointerCount;

      WirePointer* tag = reinterpret_cast<WirePointer*>(ptr);
      tag->setKindAndInlineCompositeListElementCount(WirePointer::STRUCT, value.elementCount);
      tag->structRef.set(dataSize, pointerCount);
      ptr += POINTER_SIZE_IN_WORDS;

      const word* src = reinterpret_cast<const word*>(value.ptr);
      for (uint i = 0; i < value.elementCount / ELEMENTS; i++) {
        memcpy(ptr, src, value.structDataSize / BITS_PER_BYTE / BYTES);
        ptr += dataSize;
        src += dataSize;

        for (uint j = 0; j < pointerCount / POINTERS; j++) {
          setObjectPointer(segment, reinterpret_cast<WirePointer*>(ptr), readObjectPointer(
              value.segment, reinterpret_cast<const WirePointer*>(src), nullptr,
              value.nestingLimit));
          ptr += POINTER_SIZE_IN_WORDS;
          src += POINTER_SIZE_IN_WORDS;
        }
      }

      return ptr;
    }
  }

  static KJ_ALWAYS_INLINE(void setObjectPointer(
      SegmentBuilder* segment, WirePointer* ref, ObjectReader value)) {
    switch (value.kind) {
      case ObjectKind::NULL_POINTER:
        memset(ref, 0, sizeof(*ref));
        break;
      case ObjectKind::STRUCT:
        setStructPointer(segment, ref, value.structReader);
        break;
      case ObjectKind::LIST:
        setListPointer(segment, ref, value.listReader);
        break;
    }
  }

  static void adopt(SegmentBuilder* segment, WirePointer* ref, OrphanBuilder&& value) {
    KJ_REQUIRE(value.segment->getArena() == segment->getArena(),
               "Adopted object must live in the same message.");

    if (!ref->isNull()) {
      zeroObject(segment, ref);
    }

    if (value.segment == nullptr) {
      // Set null.
      memset(ref, 0, sizeof(*ref));
    } else if (value.tagAsPtr()->kind() == WirePointer::FAR) {
      // FAR pointers are position-independent, so we can just copy.
      memcpy(ref, value.tagAsPtr(), sizeof(WirePointer));
    } else {
      WireHelpers::transferPointer(segment, ref, value.segment, value.tagAsPtr(), value.location);
    }

    // Take ownership away from the OrphanBuilder.
    memset(value.tagAsPtr(), 0, sizeof(WirePointer));
    value.location = nullptr;
    value.segment = nullptr;
  }

  static OrphanBuilder disown(SegmentBuilder* segment, WirePointer* ref) {
    OrphanBuilder result(ref, segment,
        ref->kind() == WirePointer::FAR ? nullptr : ref->target());
    memset(ref, 0, sizeof(*ref));
    return result;
  }

  // -----------------------------------------------------------------

  static KJ_ALWAYS_INLINE(StructReader readStructPointer(
      SegmentReader* segment, const WirePointer* ref, const word* defaultValue,
      int nestingLimit)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return StructReader();
      }
      segment = nullptr;
      ref = reinterpret_cast<const WirePointer*>(defaultValue);
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    KJ_REQUIRE(nestingLimit > 0,
               "Message is too deeply-nested or contains cycles.  See capnp::ReadOptions.") {
      goto useDefault;
    }

    const word* ptr = followFars(ref, segment);
    if (KJ_UNLIKELY(ptr == nullptr)) {
      // Already reported the error.
      goto useDefault;
    }

    KJ_REQUIRE(ref->kind() == WirePointer::STRUCT,
               "Message contains non-struct pointer where struct pointer was expected.") {
      goto useDefault;
    }

    KJ_REQUIRE(boundsCheck(segment, ptr, ptr + ref->structRef.wordSize()),
               "Message contained out-of-bounds struct pointer.") {
      goto useDefault;
    }

    return StructReader(
        segment, ptr, reinterpret_cast<const WirePointer*>(ptr + ref->structRef.dataSize.get()),
        ref->structRef.dataSize.get() * BITS_PER_WORD,
        ref->structRef.ptrCount.get(),
        0 * BITS, nestingLimit - 1);
  }

  static KJ_ALWAYS_INLINE(ListReader readListPointer(
      SegmentReader* segment, const WirePointer* ref, const word* defaultValue,
      FieldSize expectedElementSize, int nestingLimit)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return ListReader();
      }
      segment = nullptr;
      ref = reinterpret_cast<const WirePointer*>(defaultValue);
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    KJ_REQUIRE(nestingLimit > 0,
               "Message is too deeply-nested or contains cycles.  See capnp::ReadOptions.") {
      goto useDefault;
    }

    const word* ptr = followFars(ref, segment);
    if (KJ_UNLIKELY(ptr == nullptr)) {
      // Already reported error.
      goto useDefault;
    }

    KJ_REQUIRE(ref->kind() == WirePointer::LIST,
               "Message contains non-list pointer where list pointer was expected.") {
      goto useDefault;
    }

    if (ref->listRef.elementSize() == FieldSize::INLINE_COMPOSITE) {
      decltype(WORDS/ELEMENTS) wordsPerElement;
      ElementCount size;

      WordCount wordCount = ref->listRef.inlineCompositeWordCount();

      // An INLINE_COMPOSITE list points to a tag, which is formatted like a pointer.
      const WirePointer* tag = reinterpret_cast<const WirePointer*>(ptr);
      ptr += POINTER_SIZE_IN_WORDS;

      KJ_REQUIRE(boundsCheck(segment, ptr - POINTER_SIZE_IN_WORDS, ptr + wordCount),
                 "Message contains out-of-bounds list pointer.") {
        goto useDefault;
      }

      KJ_REQUIRE(tag->kind() == WirePointer::STRUCT,
                 "INLINE_COMPOSITE lists of non-STRUCT type are not supported.") {
        goto useDefault;
      }

      size = tag->inlineCompositeListElementCount();
      wordsPerElement = tag->structRef.wordSize() / ELEMENTS;

      KJ_REQUIRE(size * wordsPerElement <= wordCount,
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
          KJ_FAIL_REQUIRE("Expected a bit list, but got a list of structs.") {
            goto useDefault;
          }
          break;

        case FieldSize::BYTE:
        case FieldSize::TWO_BYTES:
        case FieldSize::FOUR_BYTES:
        case FieldSize::EIGHT_BYTES:
          KJ_REQUIRE(tag->structRef.dataSize.get() > 0 * WORDS,
                     "Expected a primitive list, but got a list of pointer-only structs.") {
            goto useDefault;
          }
          break;

        case FieldSize::POINTER:
          // We expected a list of pointers but got a list of structs.  Assuming the first field
          // in the struct is the pointer we were looking for, we want to munge the pointer to
          // point at the first element's pointer section.
          ptr += tag->structRef.dataSize.get();
          KJ_REQUIRE(tag->structRef.ptrCount.get() > 0 * POINTERS,
                     "Expected a pointer list, but got a list of data-only structs.") {
            goto useDefault;
          }
          break;

        case FieldSize::INLINE_COMPOSITE:
          break;
      }

      return ListReader(
          segment, ptr, size, wordsPerElement * BITS_PER_WORD,
          tag->structRef.dataSize.get() * BITS_PER_WORD,
          tag->structRef.ptrCount.get(), nestingLimit - 1);

    } else {
      // This is a primitive or pointer list, but all such lists can also be interpreted as struct
      // lists.  We need to compute the data size and pointer count for such structs.
      BitCount dataSize = dataBitsPerElement(ref->listRef.elementSize()) * ELEMENTS;
      WirePointerCount pointerCount =
          pointersPerElement(ref->listRef.elementSize()) * ELEMENTS;
      auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;

      KJ_REQUIRE(boundsCheck(segment, ptr, ptr +
                     roundBitsUpToWords(ElementCount64(ref->listRef.elementCount()) * step)),
                 "Message contains out-of-bounds list pointer.") {
        goto useDefault;
      }

      // Verify that the elements are at least as large as the expected type.  Note that if we
      // expected INLINE_COMPOSITE, the expected sizes here will be zero, because bounds checking
      // will be performed at field access time.  So this check here is for the case where we
      // expected a list of some primitive or pointer type.

      BitCount expectedDataBitsPerElement =
          dataBitsPerElement(expectedElementSize) * ELEMENTS;
      WirePointerCount expectedPointersPerElement =
          pointersPerElement(expectedElementSize) * ELEMENTS;

      KJ_REQUIRE(expectedDataBitsPerElement <= dataSize,
                 "Message contained list with incompatible element type.") {
        goto useDefault;
      }
      KJ_REQUIRE(expectedPointersPerElement <= pointerCount,
                 "Message contained list with incompatible element type.") {
        goto useDefault;
      }

      return ListReader(segment, ptr, ref->listRef.elementCount(), step,
                        dataSize, pointerCount, nestingLimit - 1);
    }
  }

  static KJ_ALWAYS_INLINE(Text::Reader readTextPointer(
      SegmentReader* segment, const WirePointer* ref,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr) defaultValue = "";
      return Text::Reader(reinterpret_cast<const char*>(defaultValue), defaultSize / BYTES);
    } else {
      const word* ptr = followFars(ref, segment);

      if (KJ_UNLIKELY(ptr == nullptr)) {
        // Already reported error.
        goto useDefault;
      }

      uint size = ref->listRef.elementCount() / ELEMENTS;

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
                 "Message contains non-list pointer where text was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(ref->listRef.elementSize() == FieldSize::BYTE,
                 "Message contains list pointer of non-bytes where text was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(boundsCheck(segment, ptr, ptr +
                     roundBytesUpToWords(ref->listRef.elementCount() * (1 * BYTES / ELEMENTS))),
                 "Message contained out-of-bounds text pointer.") {
        goto useDefault;
      }

      KJ_REQUIRE(size > 0, "Message contains text that is not NUL-terminated.") {
        goto useDefault;
      }

      const char* cptr = reinterpret_cast<const char*>(ptr);
      --size;  // NUL terminator

      KJ_REQUIRE(cptr[size] == '\0', "Message contains text that is not NUL-terminated.") {
        goto useDefault;
      }

      return Text::Reader(cptr, size);
    }
  }

  static KJ_ALWAYS_INLINE(Data::Reader readDataPointer(
      SegmentReader* segment, const WirePointer* ref,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref == nullptr || ref->isNull()) {
    useDefault:
      return Data::Reader(reinterpret_cast<const byte*>(defaultValue), defaultSize / BYTES);
    } else {
      const word* ptr = followFars(ref, segment);

      if (KJ_UNLIKELY(ptr == nullptr)) {
        // Already reported error.
        goto useDefault;
      }

      uint size = ref->listRef.elementCount() / ELEMENTS;

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
                 "Message contains non-list pointer where data was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(ref->listRef.elementSize() == FieldSize::BYTE,
                 "Message contains list pointer of non-bytes where data was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(boundsCheck(segment, ptr, ptr +
                     roundBytesUpToWords(ref->listRef.elementCount() * (1 * BYTES / ELEMENTS))),
                 "Message contained out-of-bounds data pointer.") {
        goto useDefault;
      }

      return Data::Reader(reinterpret_cast<const byte*>(ptr), size);
    }
  }

  static ObjectReader readObjectPointer(
      SegmentReader* segment, const WirePointer* ref,
      const word* defaultValue, int nestingLimit) {
    // We can't really reuse readStructPointer() and readListPointer() because they are designed
    // for the case where we are expecting a specific type, and they do validation around that,
    // whereas this method is for the case where we accept any pointer.
    //
    // Not always-inline because it is called from several places in the copying code, and anyway
    // is relatively rarely used.

    if (ref == nullptr || ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return ObjectReader();
      }
      segment = nullptr;
      ref = reinterpret_cast<const WirePointer*>(defaultValue);
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    const word* ptr = WireHelpers::followFars(ref, segment);
    if (KJ_UNLIKELY(ptr == nullptr)) {
      // Already reported the error.
      goto useDefault;
    }

    switch (ref->kind()) {
      case WirePointer::STRUCT:
        KJ_REQUIRE(nestingLimit > 0,
              "Message is too deeply-nested or contains cycles.  See capnp::ReadOptions.") {
          goto useDefault;
        }

        KJ_REQUIRE(boundsCheck(segment, ptr, ptr + ref->structRef.wordSize()),
                   "Message contained out-of-bounds struct pointer.") {
          goto useDefault;
        }
        return ObjectReader(
            StructReader(segment, ptr,
                         reinterpret_cast<const WirePointer*>(ptr + ref->structRef.dataSize.get()),
                         ref->structRef.dataSize.get() * BITS_PER_WORD,
                         ref->structRef.ptrCount.get(),
                         0 * BITS, nestingLimit - 1));
      case WirePointer::LIST: {
        FieldSize elementSize = ref->listRef.elementSize();

        KJ_REQUIRE(nestingLimit > 0,
              "Message is too deeply-nested or contains cycles.  See capnp::ReadOptions.") {
          goto useDefault;
        }

        if (elementSize == FieldSize::INLINE_COMPOSITE) {
          WordCount wordCount = ref->listRef.inlineCompositeWordCount();
          const WirePointer* tag = reinterpret_cast<const WirePointer*>(ptr);
          ptr += POINTER_SIZE_IN_WORDS;

          KJ_REQUIRE(boundsCheck(segment, ptr - POINTER_SIZE_IN_WORDS, ptr + wordCount),
                     "Message contains out-of-bounds list pointer.") {
            goto useDefault;
          }

          KJ_REQUIRE(tag->kind() == WirePointer::STRUCT,
                     "INLINE_COMPOSITE lists of non-STRUCT type are not supported.") {
            goto useDefault;
          }

          ElementCount elementCount = tag->inlineCompositeListElementCount();
          auto wordsPerElement = tag->structRef.wordSize() / ELEMENTS;

          KJ_REQUIRE(wordsPerElement * elementCount <= wordCount,
                     "INLINE_COMPOSITE list's elements overrun its word count.") {
            goto useDefault;
          }

          return ObjectReader(
              ListReader(segment, ptr, elementCount, wordsPerElement * BITS_PER_WORD,
                         tag->structRef.dataSize.get() * BITS_PER_WORD,
                         tag->structRef.ptrCount.get(), nestingLimit - 1));
        } else {
          BitCount dataSize = dataBitsPerElement(elementSize) * ELEMENTS;
          WirePointerCount pointerCount = pointersPerElement(elementSize) * ELEMENTS;
          auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;
          ElementCount elementCount = ref->listRef.elementCount();
          WordCount wordCount = roundBitsUpToWords(ElementCount64(elementCount) * step);

          KJ_REQUIRE(boundsCheck(segment, ptr, ptr + wordCount),
                     "Message contains out-of-bounds list pointer.") {
            goto useDefault;
          }

          return ObjectReader(
              ListReader(segment, ptr, elementCount, step, dataSize, pointerCount,
                         nestingLimit - 1));
        }
      }
      default:
        KJ_FAIL_REQUIRE("Message contained invalid pointer.") {
          goto useDefault;
        }
    }
  }
};

// =======================================================================================
// StructBuilder

StructBuilder StructBuilder::initRoot(
    SegmentBuilder* segment, word* location, StructSize size) {
  return WireHelpers::initStructPointer(
      reinterpret_cast<WirePointer*>(location), segment, size);
}

void StructBuilder::setRoot(SegmentBuilder* segment, word* location, StructReader value) {
  WireHelpers::setStructPointer(segment, reinterpret_cast<WirePointer*>(location), value);
}

StructBuilder StructBuilder::getRoot(
    SegmentBuilder* segment, word* location, StructSize size) {
  return WireHelpers::getWritableStructPointer(
      reinterpret_cast<WirePointer*>(location), segment, size, nullptr);
}

StructBuilder StructBuilder::initStructField(
    WirePointerCount ptrIndex, StructSize size) {
  return WireHelpers::initStructPointer(pointers + ptrIndex, segment, size);
}

StructBuilder StructBuilder::getStructField(
    WirePointerCount ptrIndex, StructSize size, const word* defaultValue) {
  return WireHelpers::getWritableStructPointer(
      pointers + ptrIndex, segment, size, defaultValue);
}

ListBuilder StructBuilder::initListField(
    WirePointerCount ptrIndex, FieldSize elementSize, ElementCount elementCount) {
  return WireHelpers::initListPointer(
      pointers + ptrIndex, segment,
      elementCount, elementSize);
}

ListBuilder StructBuilder::initStructListField(
    WirePointerCount ptrIndex, ElementCount elementCount, StructSize elementSize) {
  return WireHelpers::initStructListPointer(
      pointers + ptrIndex, segment, elementCount, elementSize);
}

ListBuilder StructBuilder::getListField(
    WirePointerCount ptrIndex, FieldSize elementSize, const word* defaultValue) {
  return WireHelpers::getWritableListPointer(
      pointers + ptrIndex, segment, elementSize, defaultValue);
}

ListBuilder StructBuilder::getStructListField(
    WirePointerCount ptrIndex, StructSize elementSize, const word* defaultValue) {
  return WireHelpers::getWritableStructListPointer(
      pointers + ptrIndex, segment, elementSize, defaultValue);
}

template <>
Text::Builder StructBuilder::initBlobField<Text>(WirePointerCount ptrIndex, ByteCount size) {
  return WireHelpers::initTextPointer(pointers + ptrIndex, segment, size);
}
template <>
void StructBuilder::setBlobField<Text>(WirePointerCount ptrIndex, Text::Reader value) {
  WireHelpers::setTextPointer(pointers + ptrIndex, segment, value);
}
template <>
Text::Builder StructBuilder::getBlobField<Text>(
    WirePointerCount ptrIndex, const void* defaultValue, ByteCount defaultSize) {
  return WireHelpers::getWritableTextPointer(
      pointers + ptrIndex, segment, defaultValue, defaultSize);
}

template <>
Data::Builder StructBuilder::initBlobField<Data>(WirePointerCount ptrIndex, ByteCount size) {
  return WireHelpers::initDataPointer(pointers + ptrIndex, segment, size);
}
template <>
void StructBuilder::setBlobField<Data>(WirePointerCount ptrIndex, Data::Reader value) {
  WireHelpers::setDataPointer(pointers + ptrIndex, segment, value);
}
template <>
Data::Builder StructBuilder::getBlobField<Data>(
    WirePointerCount ptrIndex, const void* defaultValue, ByteCount defaultSize) {
  return WireHelpers::getWritableDataPointer(
      pointers + ptrIndex, segment, defaultValue, defaultSize);
}

ObjectBuilder StructBuilder::getObjectField(
    WirePointerCount ptrIndex, const word* defaultValue) {
  return WireHelpers::getWritableObjectPointer(segment, pointers + ptrIndex, defaultValue);
}

void StructBuilder::setStructField(WirePointerCount ptrIndex, StructReader value) {
  WireHelpers::setStructPointer(segment, pointers + ptrIndex, value);
}

void StructBuilder::setListField(WirePointerCount ptrIndex, ListReader value) {
  WireHelpers::setListPointer(segment, pointers + ptrIndex, value);
}

void StructBuilder::setObjectField(WirePointerCount ptrIndex, ObjectReader value) {
  return WireHelpers::setObjectPointer(segment, pointers + ptrIndex, value);
}

void StructBuilder::adopt(WirePointerCount ptrIndex, OrphanBuilder&& value) {
  WireHelpers::adopt(segment, pointers + ptrIndex, kj::mv(value));
}

OrphanBuilder StructBuilder::disown(WirePointerCount ptrIndex) {
  return WireHelpers::disown(segment, pointers + ptrIndex);
}

void StructBuilder::transferContentFrom(StructBuilder other) {
  // Determine the amount of data the builders have in common.
  BitCount sharedDataSize = kj::min(dataSize, other.dataSize);

  if (dataSize > sharedDataSize) {
    // Since the target is larger than the source, make sure to zero out the extra bits that the
    // source doesn't have.
    if (dataSize == 1 * BITS) {
      setDataField<bool>(0 * ELEMENTS, false);
    } else {
      byte* unshared = reinterpret_cast<byte*>(data) + sharedDataSize / BITS_PER_BYTE / BYTES;
      memset(unshared, 0, (dataSize - sharedDataSize) / BITS_PER_BYTE / BYTES);
    }
  }

  // Copy over the shared part.
  if (sharedDataSize == 1 * BITS) {
    setDataField<bool>(0 * ELEMENTS, other.getDataField<bool>(0 * ELEMENTS));
  } else {
    memcpy(data, other.data, sharedDataSize / BITS_PER_BYTE / BYTES);
  }

  // Zero out all pointers in the target.
  for (uint i = 0; i < pointerCount / POINTERS; i++) {
    WireHelpers::zeroObject(segment, pointers + i);
  }

  // Transfer the pointers.
  WirePointerCount sharedPointerCount = kj::min(pointerCount, other.pointerCount);
  for (uint i = 0; i < sharedPointerCount / POINTERS; i++) {
    WireHelpers::transferPointer(segment, pointers + i, other.segment, other.pointers + i);
  }

  // Zero out the pointers that were transferred in the source because it no longer has ownership.
  // If the source had any extra pointers that the destination didn't have space for, we
  // intentionally leave them be, so that they'll be cleaned up later.
  memset(other.pointers, 0, sharedPointerCount * BYTES_PER_POINTER / BYTES);
}

bool StructBuilder::isPointerFieldNull(WirePointerCount ptrIndex) {
  return (pointers + ptrIndex)->isNull();
}

StructReader StructBuilder::asReader() const {
  return StructReader(segment, data, pointers,
      dataSize, pointerCount, bit0Offset, std::numeric_limits<int>::max());
}

BuilderArena* StructBuilder::getArena() {
  return segment->getArena();
}

// =======================================================================================
// StructReader

StructReader StructReader::readRootUnchecked(const word* location) {
  return WireHelpers::readStructPointer(nullptr, reinterpret_cast<const WirePointer*>(location),
                                        nullptr, std::numeric_limits<int>::max());
}

StructReader StructReader::readRoot(
    const word* location, SegmentReader* segment, int nestingLimit) {
  KJ_REQUIRE(WireHelpers::boundsCheck(segment, location, location + POINTER_SIZE_IN_WORDS),
             "Root location out-of-bounds.") {
    location = nullptr;
  }

  return WireHelpers::readStructPointer(segment, reinterpret_cast<const WirePointer*>(location),
                                        nullptr, nestingLimit);
}

StructReader StructReader::getStructField(
    WirePointerCount ptrIndex, const word* defaultValue) const {
  const WirePointer* ref = ptrIndex >= pointerCount ? nullptr : pointers + ptrIndex;
  return WireHelpers::readStructPointer(segment, ref, defaultValue, nestingLimit);
}

ListReader StructReader::getListField(
    WirePointerCount ptrIndex, FieldSize expectedElementSize, const word* defaultValue) const {
  const WirePointer* ref = ptrIndex >= pointerCount ? nullptr : pointers + ptrIndex;
  return WireHelpers::readListPointer(
      segment, ref, defaultValue, expectedElementSize, nestingLimit);
}

template <>
Text::Reader StructReader::getBlobField<Text>(
    WirePointerCount ptrIndex, const void* defaultValue, ByteCount defaultSize) const {
  const WirePointer* ref = ptrIndex >= pointerCount ? nullptr : pointers + ptrIndex;
  return WireHelpers::readTextPointer(segment, ref, defaultValue, defaultSize);
}

template <>
Data::Reader StructReader::getBlobField<Data>(
    WirePointerCount ptrIndex, const void* defaultValue, ByteCount defaultSize) const {
  const WirePointer* ref = ptrIndex >= pointerCount ? nullptr : pointers + ptrIndex;
  return WireHelpers::readDataPointer(segment, ref, defaultValue, defaultSize);
}

ObjectReader StructReader::getObjectField(
    WirePointerCount ptrIndex, const word* defaultValue) const {
  return WireHelpers::readObjectPointer(
      segment, pointers + ptrIndex, defaultValue, nestingLimit);
}

const word* StructReader::getUncheckedPointer(WirePointerCount ptrIndex) const {
  KJ_REQUIRE(segment == nullptr, "getUncheckedPointer() only allowed on unchecked messages.");
  return reinterpret_cast<const word*>(pointers + ptrIndex);
}

bool StructReader::isPointerFieldNull(WirePointerCount ptrIndex) const {
  return ptrIndex >= pointerCount || (pointers + ptrIndex)->isNull();
}

WordCount64 StructReader::totalSize() const {
  WordCount64 result = WireHelpers::roundBitsUpToWords(dataSize) + pointerCount * WORDS_PER_POINTER;

  for (uint i = 0; i < pointerCount / POINTERS; i++) {
    result += WireHelpers::totalSize(segment, pointers + i, nestingLimit);
  }

  if (segment != nullptr) {
    // This traversal should not count against the read limit, because it's highly likely that
    // the caller is going to traverse the object again, e.g. to copy it.
    segment->unread(result);
  }

  return result;
}

// =======================================================================================
// ListBuilder

Text::Builder ListBuilder::asText() {
  KJ_REQUIRE(structDataSize == 8 * BITS && structPointerCount == 0 * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Text::Builder();
  }

  size_t size = elementCount / ELEMENTS;

  KJ_REQUIRE(size > 0, "Message contains text that is not NUL-terminated.") {
    return Text::Builder();
  }

  char* cptr = reinterpret_cast<char*>(ptr);
  --size;  // NUL terminator

  KJ_REQUIRE(cptr[size] == '\0', "Message contains text that is not NUL-terminated.") {
    return Text::Builder();
  }

  return Text::Builder(cptr, size);
}

Data::Builder ListBuilder::asData() {
  KJ_REQUIRE(structDataSize == 8 * BITS && structPointerCount == 0 * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Data::Builder();
  }

  return Data::Builder(reinterpret_cast<byte*>(ptr), elementCount / ELEMENTS);
}

StructBuilder ListBuilder::getStructElement(ElementCount index) {
  BitCount64 indexBit = ElementCount64(index) * step;
  byte* structData = ptr + indexBit / BITS_PER_BYTE;
  return StructBuilder(segment, structData,
      reinterpret_cast<WirePointer*>(structData + structDataSize / BITS_PER_BYTE),
      structDataSize, structPointerCount, indexBit % BITS_PER_BYTE);
}

ListBuilder ListBuilder::initListElement(
    ElementCount index, FieldSize elementSize, ElementCount elementCount) {
  return WireHelpers::initListPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE),
      segment, elementCount, elementSize);
}

ListBuilder ListBuilder::initStructListElement(
    ElementCount index, ElementCount elementCount, StructSize elementSize) {
  return WireHelpers::initStructListPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE),
      segment, elementCount, elementSize);
}

ListBuilder ListBuilder::getListElement(ElementCount index, FieldSize elementSize) {
  return WireHelpers::getWritableListPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment,
      elementSize, nullptr);
}

ListBuilder ListBuilder::getStructListElement(ElementCount index, StructSize elementSize) {
  return WireHelpers::getWritableStructListPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment,
      elementSize, nullptr);
}

template <>
Text::Builder ListBuilder::initBlobElement<Text>(ElementCount index, ByteCount size) {
  return WireHelpers::initTextPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment, size);
}
template <>
void ListBuilder::setBlobElement<Text>(ElementCount index, Text::Reader value) {
  WireHelpers::setTextPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment, value);
}
template <>
Text::Builder ListBuilder::getBlobElement<Text>(ElementCount index) {
  return WireHelpers::getWritableTextPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment, "", 0 * BYTES);
}

template <>
Data::Builder ListBuilder::initBlobElement<Data>(ElementCount index, ByteCount size) {
  return WireHelpers::initDataPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment, size);
}
template <>
void ListBuilder::setBlobElement<Data>(ElementCount index, Data::Reader value) {
  WireHelpers::setDataPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment, value);
}
template <>
Data::Builder ListBuilder::getBlobElement<Data>(ElementCount index) {
  return WireHelpers::getWritableDataPointer(
      reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), segment, nullptr,
      0 * BYTES);
}

ObjectBuilder ListBuilder::getObjectElement(ElementCount index) {
  return WireHelpers::getWritableObjectPointer(
      segment, reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), nullptr);
}

void ListBuilder::setListElement(ElementCount index, ListReader value) {
  WireHelpers::setListPointer(
      segment, reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), value);
}

void ListBuilder::setObjectElement(ElementCount index, ObjectReader value) {
  return WireHelpers::setObjectPointer(
      segment, reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), value);
}

void ListBuilder::adopt(ElementCount index, OrphanBuilder&& value) {
  WireHelpers::adopt(
      segment, reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE), kj::mv(value));
}

OrphanBuilder ListBuilder::disown(ElementCount index) {
  return WireHelpers::disown(
      segment, reinterpret_cast<WirePointer*>(ptr + index * step / BITS_PER_BYTE));
}

ListReader ListBuilder::asReader() const {
  return ListReader(segment, ptr, elementCount, step, structDataSize, structPointerCount,
                    std::numeric_limits<int>::max());
}

BuilderArena* ListBuilder::getArena() {
  return segment->getArena();
}

// =======================================================================================
// ListReader

Text::Reader ListReader::asText() {
  KJ_REQUIRE(structDataSize == 8 * BITS && structPointerCount == 0 * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Text::Reader();
  }

  size_t size = elementCount / ELEMENTS;

  KJ_REQUIRE(size > 0, "Message contains text that is not NUL-terminated.") {
    return Text::Reader();
  }

  const char* cptr = reinterpret_cast<const char*>(ptr);
  --size;  // NUL terminator

  KJ_REQUIRE(cptr[size] == '\0', "Message contains text that is not NUL-terminated.") {
    return Text::Reader();
  }

  return Text::Reader(cptr, size);
}

Data::Reader ListReader::asData() {
  KJ_REQUIRE(structDataSize == 8 * BITS && structPointerCount == 0 * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Data::Reader();
  }

  return Data::Reader(reinterpret_cast<const byte*>(ptr), elementCount / ELEMENTS);
}

StructReader ListReader::getStructElement(ElementCount index) const {
  KJ_REQUIRE(nestingLimit > 0,
             "Message is too deeply-nested or contains cycles.  See capnp::ReadOptions.") {
    return StructReader();
  }

  BitCount64 indexBit = ElementCount64(index) * step;
  const byte* structData = ptr + indexBit / BITS_PER_BYTE;
  const WirePointer* structPointers =
      reinterpret_cast<const WirePointer*>(structData + structDataSize / BITS_PER_BYTE);

  // This check should pass if there are no bugs in the list pointer validation code.
  KJ_DASSERT(structPointerCount == 0 * POINTERS ||
         (uintptr_t)structPointers % sizeof(WirePointer) == 0,
         "Pointer section of struct list element not aligned.");

  return StructReader(
      segment, structData, structPointers,
      structDataSize, structPointerCount,
      indexBit % BITS_PER_BYTE, nestingLimit - 1);
}

static const WirePointer* checkAlignment(const void* ptr) {
  KJ_DASSERT((uintptr_t)ptr % sizeof(WirePointer) == 0,
         "Pointer section of struct list element not aligned.");
  return reinterpret_cast<const WirePointer*>(ptr);
}

ListReader ListReader::getListElement(
    ElementCount index, FieldSize expectedElementSize) const {
  return WireHelpers::readListPointer(
      segment, checkAlignment(ptr + index * step / BITS_PER_BYTE),
      nullptr, expectedElementSize, nestingLimit);
}

template <>
Text::Reader ListReader::getBlobElement<Text>(ElementCount index) const {
  return WireHelpers::readTextPointer(
      segment, checkAlignment(ptr + index * step / BITS_PER_BYTE),
      "", 0 * BYTES);
}

template <>
Data::Reader ListReader::getBlobElement<Data>(ElementCount index) const {
  return WireHelpers::readDataPointer(
      segment, checkAlignment(ptr + index * step / BITS_PER_BYTE),
      nullptr, 0 * BYTES);
}

ObjectReader ListReader::getObjectElement(ElementCount index) const {
  return WireHelpers::readObjectPointer(
      segment, checkAlignment(ptr + index * step / BITS_PER_BYTE), nullptr, nestingLimit);
}

// =======================================================================================
// OrphanBuilder

// TODO(cleanup):  This is hacky.  In order to reuse WireHelpers in OrphanBuilder::init*() and
//   OrphanBuilder::copy*(), we actually pass them pointers to WirePointers allocated on the stack.
//   When this pointer is initialized, the offset may be truncated and thus end up being garbage.
//   This is OK because we define the offset to be ignored in this case, but there is a fair amount
//   of non-local reasoning going on.  Additionally, in order to select a segment, these methods
//   manually compute the size that they expect WireHelpers to allocate.  This is redundant and
//   could theoretically get out-of-sync with the way WireHelpers computes them, leading to subtle
//   bugs.  Some refactoring could make this cleaner, perhaps, but I couldn't think of a reasonably
//   non-invasive approach.

OrphanBuilder OrphanBuilder::initStruct(BuilderArena* arena, StructSize size) {
  OrphanBuilder result;
  result.segment = arena->getSegmentWithAvailable(size.total());
  StructBuilder builder = WireHelpers::initStructPointer(result.tagAsPtr(), result.segment, size);
  KJ_ASSERT(builder.segment == result.segment,
            "Orphan was unexpectedly allocated in a different segment.");
  result.location = reinterpret_cast<word*>(builder.data);
  return result;
}

OrphanBuilder OrphanBuilder::initList(
    BuilderArena* arena, ElementCount elementCount, FieldSize elementSize) {
  KJ_DREQUIRE(elementSize != FieldSize::INLINE_COMPOSITE,
              "Use OrphanBuilder::initStructList() instead.");

  decltype(BITS / ELEMENTS) bitsPerElement = dataBitsPerElement(elementSize) +
      pointersPerElement(elementSize) * BITS_PER_POINTER;

  OrphanBuilder result;
  result.segment = arena->getSegmentWithAvailable(
      WireHelpers::roundBitsUpToWords(bitsPerElement * ElementCount64(elementCount)));
  ListBuilder builder =
      WireHelpers::initListPointer(result.tagAsPtr(), result.segment, elementCount, elementSize);
  KJ_ASSERT(builder.segment == result.segment,
            "Orphan was unexpectedly allocated in a different segment.");
  result.location = reinterpret_cast<word*>(builder.ptr);
  return result;
}

OrphanBuilder OrphanBuilder::initStructList(
    BuilderArena* arena, ElementCount elementCount, StructSize elementSize) {
  if (elementSize.preferredListEncoding != FieldSize::INLINE_COMPOSITE) {
    // Small data-only struct.  Allocate a list of primitives instead.
    return initList(arena, elementCount, elementSize.preferredListEncoding);
  } else {
    OrphanBuilder result;
    result.segment = arena->getSegmentWithAvailable(
        elementCount * (elementSize.total() / ELEMENTS) + POINTER_SIZE_IN_WORDS);
    ListBuilder builder = WireHelpers::initStructListPointer(
        result.tagAsPtr(), result.segment, elementCount, elementSize);
    KJ_ASSERT(builder.segment == result.segment,
              "Orphan was unexpectedly allocated in a different segment.");
    result.location = reinterpret_cast<word*>(builder.ptr);
    return result;
  }
}

OrphanBuilder OrphanBuilder::initText(BuilderArena* arena, ByteCount size) {
  OrphanBuilder result;
  result.segment = arena->getSegmentWithAvailable(
      WireHelpers::roundBytesUpToWords(size + 1 * BYTES));
  Text::Builder builder = WireHelpers::initTextPointer(result.tagAsPtr(), result.segment, size);
  result.location = reinterpret_cast<word*>(builder.begin());
  KJ_ASSERT(result.segment->getOffsetTo(result.location) <= result.segment->getSize(),
            "Orphan was unexpectedly allocated in a different segment.");
  return result;
}

OrphanBuilder OrphanBuilder::initData(BuilderArena* arena, ByteCount size) {
  OrphanBuilder result;
  result.segment = arena->getSegmentWithAvailable(WireHelpers::roundBytesUpToWords(size));
  Data::Builder builder = WireHelpers::initDataPointer(result.tagAsPtr(), result.segment, size);
  result.location = reinterpret_cast<word*>(builder.begin());
  KJ_ASSERT(result.segment->getOffsetTo(result.location) <= result.segment->getSize(),
            "Orphan was unexpectedly allocated in a different segment.");
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, StructReader copyFrom) {
  OrphanBuilder result;
  result.segment = arena->getSegmentWithAvailable(
      WireHelpers::roundBitsUpToWords(copyFrom.getDataSectionSize()) +
      copyFrom.getPointerSectionSize() * WORDS_PER_POINTER);
  word* ptr = WireHelpers::setStructPointer(result.segment, result.tagAsPtr(), copyFrom);
  KJ_ASSERT(result.segment->getOffsetTo(ptr) <= result.segment->getSize(),
            "Orphan was unexpectedly allocated in a different segment.");
  result.location = reinterpret_cast<word*>(ptr);
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, ListReader copyFrom) {
  OrphanBuilder result;

  WordCount wordCount = WireHelpers::roundBitsUpToWords(
      copyFrom.step * ElementCount64(copyFrom.elementCount));
  if (copyFrom.step * ELEMENTS > BITS_PER_WORD * WORDS) {
    // This is a struct list.
    wordCount += 1 * WORDS;
  }
  result.segment = arena->getSegmentWithAvailable(wordCount);

  word* ptr = WireHelpers::setListPointer(result.segment, result.tagAsPtr(), copyFrom);
  KJ_ASSERT(result.segment->getOffsetTo(ptr) <= result.segment->getSize(),
            "Orphan was unexpectedly allocated in a different segment.");
  result.location = reinterpret_cast<word*>(ptr);
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, Text::Reader copyFrom) {
  OrphanBuilder result;
  result.segment = arena->getSegmentWithAvailable(
      WireHelpers::roundBytesUpToWords((copyFrom.size() + 1) * BYTES));
  Text::Builder text = WireHelpers::initTextPointer(
      result.tagAsPtr(), result.segment, copyFrom.size() * BYTES);
  result.location = reinterpret_cast<word*>(text.begin());
  KJ_ASSERT(result.segment->getOffsetTo(result.location) <= result.segment->getSize(),
            "Orphan was unexpectedly allocated in a different segment.");
  memcpy(text.begin(), copyFrom.begin(), copyFrom.size());
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, Data::Reader copyFrom) {
  OrphanBuilder result;
  result.segment = arena->getSegmentWithAvailable(
      WireHelpers::roundBytesUpToWords(copyFrom.size() * BYTES));
  Data::Builder data = WireHelpers::initDataPointer(
      result.tagAsPtr(), result.segment, copyFrom.size() * BYTES);
  result.location = reinterpret_cast<word*>(data.begin());
  KJ_ASSERT(result.segment->getOffsetTo(result.location) <= result.segment->getSize(),
            "Orphan was unexpectedly allocated in a different segment.");
  memcpy(data.begin(), copyFrom.begin(), copyFrom.size());
  return result;
}

StructBuilder OrphanBuilder::asStruct(StructSize size) {
  StructBuilder result = WireHelpers::getWritableStructPointer(
      tagAsPtr(), location, segment, size, nullptr);

  // Watch out, the pointer could have been updated if the object had to be relocated.
  if (tagAsPtr()->kind() == WirePointer::FAR) {
    location = nullptr;
  } else {
    location = reinterpret_cast<word*>(result.data);
  }

  return result;
}

ListBuilder OrphanBuilder::asList(FieldSize elementSize) {
  ListBuilder result = WireHelpers::getWritableListPointer(
      tagAsPtr(), location, segment, elementSize, nullptr);

  // Watch out, the pointer could have been updated if the object had to be relocated.
  if (tagAsPtr()->kind() == WirePointer::FAR) {
    location = nullptr;
  } else {
    location = reinterpret_cast<word*>(result.ptr);
  }

  return result;
}

ListBuilder OrphanBuilder::asStructList(StructSize elementSize) {
  ListBuilder result = WireHelpers::getWritableStructListPointer(
      tagAsPtr(), location, segment, elementSize, nullptr);

  // Watch out, the pointer could have been updated if the object had to be relocated.
  if (tagAsPtr()->kind() == WirePointer::FAR) {
    location = nullptr;
  } else {
    location = reinterpret_cast<word*>(result.ptr);
  }

  return result;
}

Text::Builder OrphanBuilder::asText() {
  // Never relocates.
  return WireHelpers::getWritableTextPointer(tagAsPtr(), location, segment, nullptr, 0 * BYTES);
}

Data::Builder OrphanBuilder::asData() {
  // Never relocates.
  return WireHelpers::getWritableDataPointer(tagAsPtr(), location, segment, nullptr, 0 * BYTES);
}

ObjectBuilder OrphanBuilder::asObject() {
  ObjectBuilder result = WireHelpers::getWritableObjectPointer(
      segment, tagAsPtr(), location, nullptr);

  // Watch out, the pointer could have been updated if the object had to be relocated.
  if (tagAsPtr()->kind() == WirePointer::FAR) {
    location = nullptr;
  } else {
    switch (result.kind) {
      case ObjectKind::STRUCT:
        location = reinterpret_cast<word*>(result.structBuilder.data);
        break;
      case ObjectKind::LIST:
        location = reinterpret_cast<word*>(result.listBuilder.ptr);
        break;
      case ObjectKind::NULL_POINTER:
        location = nullptr;
        break;
    }
  }

  return result;
}

void OrphanBuilder::euthanize() {
  WireHelpers::zeroObject(segment, reinterpret_cast<WirePointer*>(&tag), location);
  memset(&tag, 0, sizeof(tag));  // Use memset to comply with aliasing rules.
  segment = nullptr;
  location = nullptr;
}

}  // namespace _ (private)
}  // namespace capnp
