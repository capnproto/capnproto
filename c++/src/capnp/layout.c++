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

#define CAPNP_PRIVATE
#include "layout.h"
#include <kj/debug.h>
#include "arena.h"
#include <string.h>
#include <stdlib.h>

#if !CAPNP_LITE
#include "capability.h"
#endif  // !CAPNP_LITE

namespace capnp {
namespace _ {  // private

#if !CAPNP_LITE
static BrokenCapFactory* brokenCapFactory = nullptr;
// Horrible hack:  We need to be able to construct broken caps without any capability context,
// but we can't have a link-time dependency on libcapnp-rpc.

void setGlobalBrokenCapFactoryForLayoutCpp(BrokenCapFactory& factory) {
  // Called from capability.c++ when the capability API is used, to make sure that layout.c++
  // is ready for it.  May be called multiple times but always with the same value.
  __atomic_store_n(&brokenCapFactory, &factory, __ATOMIC_RELAXED);
}
#endif  // !CAPNP_LITE

#define G(n) guarded<n>()

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

    OTHER = 3
    // Reference has type "other".  If the next 30 bits are all zero (i.e. the lower 32 bits contain
    // only the kind OTHER) then the pointer is a capability.  All other values are reserved.
  };

  WireValue<uint32_t> offsetAndKind;

  KJ_ALWAYS_INLINE(Kind kind() const) {
    return static_cast<Kind>(offsetAndKind.get() & 3);
  }
  KJ_ALWAYS_INLINE(bool isPositional() const) {
    return (offsetAndKind.get() & 2) == 0;  // match STRUCT and LIST but not FAR or OTHER
  }
  KJ_ALWAYS_INLINE(bool isCapability() const) {
    return offsetAndKind.get() == OTHER;
  }

  KJ_ALWAYS_INLINE(word* target()) {
    return reinterpret_cast<word*>(this) + 1 + (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  KJ_ALWAYS_INLINE(const word* target() const) {
    return reinterpret_cast<const word*>(this) + 1 +
        (static_cast<int32_t>(offsetAndKind.get()) >> 2);
  }
  KJ_ALWAYS_INLINE(void setKindAndTarget(Kind kind, word* target, SegmentBuilder* segment)) {
    // Check that the target is really in the same segment, otherwise subtracting pointers is
    // undefined behavior.  As it turns out, it's undefined behavior that actually produces
    // unexpected results in a real-world situation that actually happened:  At one time,
    // OrphanBuilder's "tag" (a WirePointer) was allowed to be initialized as if it lived in
    // a particular segment when in fact it does not.  On 32-bit systems, where words might
    // only be 32-bit aligned, it's possible that the difference between `this` and `target` is
    // not a whole number of words.  But clang optimizes:
    //     (target - (word*)this - 1) << 2
    // to:
    //     (((ptrdiff_t)target - (ptrdiff_t)this - 8) >> 1)
    // So now when the pointers are not aligned the same, we can end up corrupting the bottom
    // two bits, where `kind` is stored.  For example, this turns a struct into a far pointer.
    // Ouch!
    KJ_DREQUIRE(segment->containsInterval(
        reinterpret_cast<word*>(this), reinterpret_cast<word*>(this + 1)));
    KJ_DREQUIRE(segment->containsInterval(target, target));
    offsetAndKind.set(((target - reinterpret_cast<word*>(this) - 1) << 2) | kind);
  }
  KJ_ALWAYS_INLINE(void setKindWithZeroOffset(Kind kind)) {
    offsetAndKind.set(kind);
  }
  KJ_ALWAYS_INLINE(void setKindAndTargetForEmptyStruct()) {
    // This pointer points at an empty struct.  Assuming the WirePointer itself is in-bounds, we
    // can set the target to point either at the WirePointer itself or immediately after it.  The
    // latter would cause the WirePointer to be "null" (since for an empty struct the upper 32
    // bits are going to be zero).  So we set an offset of -1, as if the struct were allocated
    // immediately before this pointer, to distinguish it from null.
    offsetAndKind.set(0xfffffffc);
  }
  KJ_ALWAYS_INLINE(void setKindForOrphan(Kind kind)) {
    // OrphanBuilder contains a WirePointer, but since it isn't located in a segment, it should
    // not have a valid offset (unless it is a FAR or OTHER pointer).  We set its offset to -1
    // because setting it to zero would mean a pointer to an empty struct would appear to be a null
    // pointer.
    KJ_DREQUIRE(isPositional());
    offsetAndKind.set(kind | 0xfffffffc);
  }

  KJ_ALWAYS_INLINE(ListElementCount inlineCompositeListElementCount() const) {
    return ((guarded(offsetAndKind.get()) >> G(2))
            & G(kj::maxValueForBits<LIST_ELEMENT_COUNT_BITS>())) * ELEMENTS;
  }
  KJ_ALWAYS_INLINE(void setKindAndInlineCompositeListElementCount(
      Kind kind, ListElementCount elementCount)) {
    offsetAndKind.set(unguardAs<uint32_t>((elementCount / ELEMENTS) << G(2)) | kind);
  }

  KJ_ALWAYS_INLINE(SegmentWordCount farPositionInSegment() const) {
    KJ_DREQUIRE(kind() == FAR,
        "positionInSegment() should only be called on FAR pointers.");
    return (guarded(offsetAndKind.get()) >> G(3)) * WORDS;
  }
  KJ_ALWAYS_INLINE(bool isDoubleFar() const) {
    KJ_DREQUIRE(kind() == FAR,
        "isDoubleFar() should only be called on FAR pointers.");
    return unguard((guarded(offsetAndKind.get()) >> G(2)) & G(1));
  }
  KJ_ALWAYS_INLINE(void setFar(bool isDoubleFar, WordCountN<29> pos)) {
    offsetAndKind.set(unguardAs<uint32_t>((pos / WORDS) << G(3)) |
                      (static_cast<uint32_t>(isDoubleFar) << 2) |
                      static_cast<uint32_t>(Kind::FAR));
  }
  KJ_ALWAYS_INLINE(void setCap(uint index)) {
    offsetAndKind.set(static_cast<uint32_t>(Kind::OTHER));
    capRef.index.set(index);
  }

  // -----------------------------------------------------------------
  // Part of pointer that depends on the kind.

  // Note:  Originally StructRef, ListRef, and FarRef were unnamed types, but this somehow
  //   tickled a bug in GCC:
  //     http://gcc.gnu.org/bugzilla/show_bug.cgi?id=58192
  struct StructRef {
    WireValue<WordCount16> dataSize;
    WireValue<WirePointerCount16> ptrCount;

    inline WordCountN<17> wordSize() const {
      return upgradeGuard<uint32_t>(dataSize.get()) + ptrCount.get() * WORDS_PER_POINTER;
    }

    KJ_ALWAYS_INLINE(void set(WordCount16 ds, WirePointerCount16 rc)) {
      dataSize.set(ds);
      ptrCount.set(rc);
    }
    KJ_ALWAYS_INLINE(void set(StructSize size)) {
      dataSize.set(size.data);
      ptrCount.set(size.pointers);
    }
  };

  struct ListRef {
    WireValue<uint32_t> elementSizeAndCount;

    KJ_ALWAYS_INLINE(ElementSize elementSize() const) {
      return static_cast<ElementSize>(elementSizeAndCount.get() & 7);
    }
    KJ_ALWAYS_INLINE(ElementCountN<29> elementCount() const) {
      return (guarded(elementSizeAndCount.get()) >> G(3)) * ELEMENTS;
    }
    KJ_ALWAYS_INLINE(WordCountN<29> inlineCompositeWordCount() const) {
      return elementCount() * (ONE * WORDS / ELEMENTS);
    }

    KJ_ALWAYS_INLINE(void set(ElementSize es, ElementCountN<29> ec)) {
      elementSizeAndCount.set(unguardAs<uint32_t>((ec / ELEMENTS) << G(3)) |
                              static_cast<int>(es));
    }

    KJ_ALWAYS_INLINE(void setInlineComposite(WordCountN<29> wc)) {
      elementSizeAndCount.set(unguardAs<uint32_t>((wc / WORDS) << G(3)) |
                              static_cast<int>(ElementSize::INLINE_COMPOSITE));
    }
  };

  struct FarRef {
    WireValue<SegmentId> segmentId;

    KJ_ALWAYS_INLINE(void set(SegmentId si)) {
      segmentId.set(si);
    }
  };

  struct CapRef {
    WireValue<uint32_t> index;
    // Index into the message's capability table.
  };

  union {
    uint32_t upper32Bits;

    StructRef structRef;

    ListRef listRef;

    FarRef farRef;

    CapRef capRef;
  };

  KJ_ALWAYS_INLINE(bool isNull() const) {
    // If the upper 32 bits are zero, this is a pointer to an empty struct.  We consider that to be
    // our "null" value.
    return (offsetAndKind.get() == 0) & (upper32Bits == 0);
  }

};
static_assert(sizeof(WirePointer) == sizeof(word),
    "capnp::WirePointer is not exactly one word.  This will probably break everything.");
static_assert(unguardAs<size_t>(POINTERS * WORDS_PER_POINTER * BYTES_PER_WORD / BYTES) ==
              sizeof(WirePointer),
    "WORDS_PER_POINTER is wrong.");
static_assert(unguardAs<size_t>(POINTERS * BYTES_PER_POINTER / BYTES) == sizeof(WirePointer),
    "BYTES_PER_POINTER is wrong.");
static_assert(unguardAs<size_t>(POINTERS * BITS_PER_POINTER / BITS_PER_BYTE / BYTES) ==
              sizeof(WirePointer),
    "BITS_PER_POINTER is wrong.");

namespace {

static const union {
  AlignedData<unguard(POINTER_SIZE_IN_WORDS / WORDS)> word;
  WirePointer pointer;
} zero = {{{0}}};

}  // namespace

// =======================================================================================

namespace {

template <typename T>
struct SegmentAnd {
  SegmentBuilder* segment;
  T value;
};

}  // namespace

struct WireHelpers {
#if CAPNP_DEBUG_TYPES
  template <uint64_t maxN, typename T>
  static KJ_ALWAYS_INLINE(
      kj::Quantity<kj::Guarded<(maxN + 7) / 8, T>, word> roundBytesUpToWords(
          kj::Quantity<kj::Guarded<maxN, T>, byte> bytes)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    return (bytes + G(7) * BYTES) / BYTES_PER_WORD;
  }

  template <uint64_t maxN, typename T>
  static KJ_ALWAYS_INLINE(
      kj::Quantity<kj::Guarded<(maxN + 7) / 8, T>, byte> roundBitsUpToBytes(
          kj::Quantity<kj::Guarded<maxN, T>, BitLabel> bits)) {
    return (bits + G(7) * BITS) / BITS_PER_BYTE;
  }

  template <uint64_t maxN, typename T>
  static KJ_ALWAYS_INLINE(
      kj::Quantity<kj::Guarded<(maxN + 63) / 64, T>, word> roundBitsUpToWords(
          kj::Quantity<kj::Guarded<maxN, T>, BitLabel> bits)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    return (bits + G(63) * BITS) / BITS_PER_WORD;
  }
#else
  static KJ_ALWAYS_INLINE(WordCount roundBytesUpToWords(ByteCount bytes)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    return (bytes + G(7) * BYTES) / BYTES_PER_WORD;
  }

  static KJ_ALWAYS_INLINE(ByteCount roundBitsUpToBytes(BitCount bits)) {
    return (bits + G(7) * BITS) / BITS_PER_BYTE;
  }

  static KJ_ALWAYS_INLINE(WordCount64 roundBitsUpToWords(BitCount64 bits)) {
    static_assert(sizeof(word) == 8, "This code assumes 64-bit words.");
    return (bits + G(63) * BITS) / BITS_PER_WORD;
  }

  static KJ_ALWAYS_INLINE(ByteCount64 roundBitsUpToBytes(BitCount64 bits)) {
    return (bits + G(7) * BITS) / BITS_PER_BYTE;
  }
#endif

  static KJ_ALWAYS_INLINE(void zeroMemory(byte* ptr, ByteCount32 count)) {
    memset(ptr, 0, unguard(count / BYTES));
  }

  static KJ_ALWAYS_INLINE(void zeroMemory(word* ptr, WordCountN<29> count)) {
    memset(ptr, 0, unguard(count * BYTES_PER_WORD / BYTES));
  }

  static KJ_ALWAYS_INLINE(void zeroMemory(WirePointer* ptr, WirePointerCountN<29> count)) {
    memset(ptr, 0, unguard(count * BYTES_PER_POINTER / BYTES));
  }

  static KJ_ALWAYS_INLINE(void copyMemory(byte* to, const byte* from, ByteCount32 count)) {
    memcpy(to, from, unguard(count / BYTES));
  }

  static KJ_ALWAYS_INLINE(void copyMemory(word* to, const word* from, WordCountN<29> count)) {
    memcpy(to, from, unguard(count * BYTES_PER_WORD / BYTES));
  }

  static KJ_ALWAYS_INLINE(void copyMemory(WirePointer* to, const WirePointer* from,
                                          WirePointerCountN<29> count)) {
    memcpy(to, from, unguard(count * BYTES_PER_POINTER  / BYTES));
  }

  static KJ_ALWAYS_INLINE(bool boundsCheck(
      SegmentReader* segment, const word* start, const word* end)) {
    // If segment is null, this is an unchecked message, so we don't do bounds checks.
    return segment == nullptr || segment->containsInterval(start, end);
  }

  static KJ_ALWAYS_INLINE(bool amplifiedRead(SegmentReader* segment, WordCount virtualAmount)) {
    // If segment is null, this is an unchecked message, so we don't do read limiter checks.
    return segment == nullptr || segment->amplifiedRead(virtualAmount);
  }

  static KJ_ALWAYS_INLINE(word* allocate(
      WirePointer*& ref, SegmentBuilder*& segment, SegmentWordCount amount,
      WirePointer::Kind kind, BuilderArena* orphanArena)) {
    // Allocate space in the message for a new object, creating far pointers if necessary.
    //
    // * `ref` starts out being a reference to the pointer which shall be assigned to point at the
    //   new object.  On return, `ref` points to a pointer which needs to be initialized with
    //   the object's type information.  Normally this is the same pointer, but it can change if
    //   a far pointer was allocated -- in this case, `ref` will end up pointing to the far
    //   pointer's tag.  Either way, `allocate()` takes care of making sure that the original
    //   pointer ends up leading to the new object.  On return, only the upper 32 bit of `*ref`
    //   need to be filled in by the caller.
    // * `segment` starts out pointing to the segment containing `ref`.  On return, it points to
    //   the segment containing the allocated object, which is usually the same segment but could
    //   be a different one if the original segment was out of space.
    // * `amount` is the number of words to allocate.
    // * `kind` is the kind of object to allocate.  It is used to initialize the pointer.  It
    //   cannot be `FAR` -- far pointers are allocated automatically as needed.
    // * `orphanArena` is usually null.  If it is non-null, then we're allocating an orphan object.
    //   In this case, `segment` starts out null; the allocation takes place in an arbitrary
    //   segment belonging to the arena.  `ref` will be initialized as a non-far pointer, but its
    //   target offset will be set to zero.

    if (orphanArena == nullptr) {
      if (!ref->isNull()) zeroObject(segment, ref);

      if (amount == ZERO * WORDS && kind == WirePointer::STRUCT) {
        // Note that the check for kind == WirePointer::STRUCT will hopefully cause this whole
        // branch to be optimized away from all the call sites that are allocating non-structs.
        ref->setKindAndTargetForEmptyStruct();
        return reinterpret_cast<word*>(ref);
      }

      word* ptr = segment->allocate(amount);

      if (ptr == nullptr) {
        // Need to allocate in a new segment.  We'll need to allocate an extra pointer worth of
        // space to act as the landing pad for a far pointer.

        WordCount amountPlusRef = amount + POINTER_SIZE_IN_WORDS;
        auto allocation = segment->getArena()->allocate(
            assertMaxBits<SEGMENT_WORD_COUNT_BITS>(amountPlusRef, []() {
              KJ_FAIL_REQUIRE("requested object size exceeds maximum segment size");
            }));
        segment = allocation.segment;
        ptr = allocation.words;

        // Set up the original pointer to be a far pointer to the new segment.
        ref->setFar(false, segment->getOffsetTo(ptr));
        ref->farRef.set(segment->getSegmentId());

        // Initialize the landing pad to indicate that the data immediately follows the pad.
        ref = reinterpret_cast<WirePointer*>(ptr);
        ref->setKindAndTarget(kind, ptr + POINTER_SIZE_IN_WORDS, segment);

        // Allocated space follows new pointer.
        return ptr + POINTER_SIZE_IN_WORDS;
      } else {
        ref->setKindAndTarget(kind, ptr, segment);
        return ptr;
      }
    } else {
      // orphanArena is non-null.  Allocate an orphan.
      KJ_DASSERT(ref->isNull());
      auto allocation = orphanArena->allocate(amount);
      segment = allocation.segment;
      ref->setKindForOrphan(kind);
      return allocation.words;
    }
  }

  static KJ_ALWAYS_INLINE(word* followFarsNoWritableCheck(
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

  static KJ_ALWAYS_INLINE(word* followFars(
      WirePointer*& ref, word* refTarget, SegmentBuilder*& segment)) {
    auto result = followFarsNoWritableCheck(ref, refTarget, segment);
    segment->checkWritable();
    return result;
  }

  static KJ_ALWAYS_INLINE(const word* followFars(
      const WirePointer*& ref, const word* refTarget, SegmentReader*& segment)) {
    // Like the other followFars() but operates on readers.

    // If the segment is null, this is an unchecked message, so there are no FAR pointers.
    if (segment != nullptr && ref->kind() == WirePointer::FAR) {
      // Look up the segment containing the landing pad.
      segment = segment->getArena()->tryGetSegment(ref->farRef.segmentId.get());
      KJ_REQUIRE(segment != nullptr, "Message contains far pointer to unknown segment.") {
        return nullptr;
      }

      // Find the landing pad and check that it is within bounds.
      const word* ptr = segment->getStartPtr() + ref->farPositionInSegment();
      WordCount padWords = guarded(1 + ref->isDoubleFar()) * POINTER_SIZE_IN_WORDS;
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
      return refTarget;
    }
  }

  // -----------------------------------------------------------------

  static void zeroObject(SegmentBuilder* segment, WirePointer* ref) {
    // Zero out the pointed-to object.  Use when the pointer is about to be overwritten making the
    // target object no longer reachable.

    // We shouldn't zero out external data linked into the message.
    if (!segment->isWritable()) return;

    switch (ref->kind()) {
      case WirePointer::STRUCT:
      case WirePointer::LIST:
        zeroObject(segment, ref, ref->target());
        break;
      case WirePointer::FAR: {
        segment = segment->getArena()->getSegment(ref->farRef.segmentId.get());
        if (segment->isWritable()) {  // Don't zero external data.
          WirePointer* pad =
              reinterpret_cast<WirePointer*>(segment->getPtrUnchecked(ref->farPositionInSegment()));

          if (ref->isDoubleFar()) {
            segment = segment->getArena()->getSegment(pad->farRef.segmentId.get());
            if (segment->isWritable()) {
              zeroObject(segment, pad + 1, segment->getPtrUnchecked(pad->farPositionInSegment()));
            }
            zeroMemory(pad, G(2) * POINTERS);
          } else {
            zeroObject(segment, pad);
            zeroMemory(pad, ONE * POINTERS);
          }
        }
        break;
      }
      case WirePointer::OTHER:
        if (ref->isCapability()) {
#if CAPNP_LITE
          KJ_FAIL_ASSERT("Capability encountered in builder in lite mode?") { break; }
#else  // CAPNP_LINE
          segment->getArena()->dropCap(ref->capRef.index.get());
#endif  // CAPNP_LITE, else
        } else {
          KJ_FAIL_REQUIRE("Unknown pointer type.") { break; }
        }
        break;
    }
  }

  static void zeroObject(SegmentBuilder* segment, WirePointer* tag, word* ptr) {
    // We shouldn't zero out external data linked into the message.
    if (!segment->isWritable()) return;

    switch (tag->kind()) {
      case WirePointer::STRUCT: {
        WirePointer* pointerSection =
            reinterpret_cast<WirePointer*>(ptr + tag->structRef.dataSize.get());
        for (auto i: kj::zeroTo(tag->structRef.ptrCount.get())) {
          zeroObject(segment, pointerSection + i);
        }
        zeroMemory(ptr, tag->structRef.wordSize());
        break;
      }
      case WirePointer::LIST: {
        switch (tag->listRef.elementSize()) {
          case ElementSize::VOID:
            // Nothing.
            break;
          case ElementSize::BIT:
          case ElementSize::BYTE:
          case ElementSize::TWO_BYTES:
          case ElementSize::FOUR_BYTES:
          case ElementSize::EIGHT_BYTES: {
            zeroMemory(ptr, roundBitsUpToWords(
                upgradeGuard<uint64_t>(tag->listRef.elementCount()) *
                dataBitsPerElement(tag->listRef.elementSize())));
            break;
          }
          case ElementSize::POINTER: {
            WirePointer* typedPtr = reinterpret_cast<WirePointer*>(ptr);
            auto count = tag->listRef.elementCount() * (ONE * POINTERS / ELEMENTS);
            for (auto i: kj::zeroTo(count)) {
              zeroObject(segment, typedPtr + i);
            }
            zeroMemory(typedPtr, count);
            break;
          }
          case ElementSize::INLINE_COMPOSITE: {
            WirePointer* elementTag = reinterpret_cast<WirePointer*>(ptr);

            KJ_ASSERT(elementTag->kind() == WirePointer::STRUCT,
                  "Don't know how to handle non-STRUCT inline composite.");
            WordCount dataSize = elementTag->structRef.dataSize.get();
            WirePointerCount pointerCount = elementTag->structRef.ptrCount.get();

            word* pos = ptr + POINTER_SIZE_IN_WORDS;
            auto count = elementTag->inlineCompositeListElementCount();
            for (auto i KJ_UNUSED: kj::zeroTo(count)) {
              pos += dataSize;

              for (auto j KJ_UNUSED: kj::zeroTo(pointerCount)) {
                zeroObject(segment, reinterpret_cast<WirePointer*>(pos));
                pos += POINTER_SIZE_IN_WORDS;
              }
            }

            auto wordsPerElement = elementTag->structRef.wordSize() / ELEMENTS;
            zeroMemory(ptr, assertMaxBits<SEGMENT_WORD_COUNT_BITS>(POINTER_SIZE_IN_WORDS +
                upgradeGuard<uint64_t>(count) * wordsPerElement, []() {
                  KJ_FAIL_ASSERT("encountered list pointer in builder which is too large to "
                      "possibly fit in a segment. Bug in builder code?");
                }));
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
      case WirePointer::OTHER:
        KJ_FAIL_ASSERT("Unexpected OTHER pointer.") {
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
      SegmentBuilder* padSegment = segment->getArena()->getSegment(ref->farRef.segmentId.get());
      if (padSegment->isWritable()) {  // Don't zero external data.
        word* pad = padSegment->getPtrUnchecked(ref->farPositionInSegment());
        memset(pad, 0, sizeof(WirePointer) * (1 + ref->isDoubleFar()));
      }
    }
    memset(ref, 0, sizeof(*ref));
  }


  // -----------------------------------------------------------------

  static MessageSizeCounts totalSize(
      SegmentReader* segment, const WirePointer* ref, int nestingLimit) {
    // Compute the total size of the object pointed to, not counting far pointer overhead.

    MessageSizeCounts result = { ZERO * WORDS, 0 };

    if (ref->isNull()) {
      return result;
    }

    KJ_REQUIRE(nestingLimit > 0, "Message is too deeply-nested.") {
      return result;
    }
    --nestingLimit;

    const word* ptr = followFars(ref, ref->target(), segment);

    switch (ref->kind()) {
      case WirePointer::STRUCT: {
        KJ_REQUIRE(boundsCheck(segment, ptr, ptr + ref->structRef.wordSize()),
                   "Message contained out-of-bounds struct pointer.") {
          return result;
        }
        result.addWords(ref->structRef.wordSize());

        const WirePointer* pointerSection =
            reinterpret_cast<const WirePointer*>(ptr + ref->structRef.dataSize.get());
        for (auto i: kj::zeroTo(ref->structRef.ptrCount.get())) {
          result += totalSize(segment, pointerSection + i, nestingLimit);
        }
        break;
      }
      case WirePointer::LIST: {
        switch (ref->listRef.elementSize()) {
          case ElementSize::VOID:
            // Nothing.
            break;
          case ElementSize::BIT:
          case ElementSize::BYTE:
          case ElementSize::TWO_BYTES:
          case ElementSize::FOUR_BYTES:
          case ElementSize::EIGHT_BYTES: {
            auto totalWords = roundBitsUpToWords(
                upgradeGuard<uint64_t>(ref->listRef.elementCount()) *
                dataBitsPerElement(ref->listRef.elementSize()));
            KJ_REQUIRE(boundsCheck(segment, ptr, ptr + totalWords),
                       "Message contained out-of-bounds list pointer.") {
              return result;
            }
            result.addWords(totalWords);
            break;
          }
          case ElementSize::POINTER: {
            WirePointerCount count = ref->listRef.elementCount() * (POINTERS / ELEMENTS);

            KJ_REQUIRE(boundsCheck(segment, ptr, ptr + count * WORDS_PER_POINTER),
                       "Message contained out-of-bounds list pointer.") {
              return result;
            }

            result.addWords(count * WORDS_PER_POINTER);

            for (auto i: kj::zeroTo(count)) {
              result += totalSize(segment, reinterpret_cast<const WirePointer*>(ptr) + i,
                                  nestingLimit);
            }
            break;
          }
          case ElementSize::INLINE_COMPOSITE: {
            auto wordCount = ref->listRef.inlineCompositeWordCount();
            KJ_REQUIRE(boundsCheck(segment, ptr, ptr + wordCount + POINTER_SIZE_IN_WORDS),
                       "Message contained out-of-bounds list pointer.") {
              return result;
            }

            result.addWords(wordCount + POINTER_SIZE_IN_WORDS);

            const WirePointer* elementTag = reinterpret_cast<const WirePointer*>(ptr);
            auto count = elementTag->inlineCompositeListElementCount();

            KJ_REQUIRE(elementTag->kind() == WirePointer::STRUCT,
                       "Don't know how to handle non-STRUCT inline composite.") {
              return result;
            }

            KJ_REQUIRE(elementTag->structRef.wordSize() / ELEMENTS * upgradeGuard<uint64_t>(count)
                       <= wordCount, "Struct list pointer's elements overran size.") {
              return result;
            }

            WordCount dataSize = elementTag->structRef.dataSize.get();
            WirePointerCount pointerCount = elementTag->structRef.ptrCount.get();

            const word* pos = ptr + POINTER_SIZE_IN_WORDS;
            for (auto i KJ_UNUSED: kj::zeroTo(count)) {
              pos += dataSize;

              for (auto j KJ_UNUSED: kj::zeroTo(pointerCount)) {
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
      case WirePointer::OTHER:
        if (ref->isCapability()) {
          result.capCount++;
        } else {
          KJ_FAIL_REQUIRE("Unknown pointer type.") { break; }
        }
        break;
    }

    return result;
  }

  // -----------------------------------------------------------------
  // Copy from an unchecked message.

  static KJ_ALWAYS_INLINE(
      void copyStruct(SegmentBuilder* segment, word* dst, const word* src,
                      StructDataWordCount dataSize, StructPointerCount pointerCount)) {
    copyMemory(dst, src, dataSize);

    const WirePointer* srcRefs = reinterpret_cast<const WirePointer*>(src + dataSize);
    WirePointer* dstRefs = reinterpret_cast<WirePointer*>(dst + dataSize);

    for (auto i: kj::zeroTo(pointerCount)) {
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
          word* dstPtr = allocate(
              dst, segment, src->structRef.wordSize(), WirePointer::STRUCT, nullptr);

          copyStruct(segment, dstPtr, srcPtr, src->structRef.dataSize.get(),
                     src->structRef.ptrCount.get());

          dst->structRef.set(src->structRef.dataSize.get(), src->structRef.ptrCount.get());
          return dstPtr;
        }
      }
      case WirePointer::LIST: {
        switch (src->listRef.elementSize()) {
          case ElementSize::VOID:
          case ElementSize::BIT:
          case ElementSize::BYTE:
          case ElementSize::TWO_BYTES:
          case ElementSize::FOUR_BYTES:
          case ElementSize::EIGHT_BYTES: {
            auto wordCount = roundBitsUpToWords(
                upgradeGuard<uint64_t>(src->listRef.elementCount()) *
                dataBitsPerElement(src->listRef.elementSize()));
            const word* srcPtr = src->target();
            word* dstPtr = allocate(dst, segment, wordCount, WirePointer::LIST, nullptr);
            copyMemory(dstPtr, srcPtr, wordCount);

            dst->listRef.set(src->listRef.elementSize(), src->listRef.elementCount());
            return dstPtr;
          }

          case ElementSize::POINTER: {
            const WirePointer* srcRefs = reinterpret_cast<const WirePointer*>(src->target());
            WirePointer* dstRefs = reinterpret_cast<WirePointer*>(
                allocate(dst, segment, src->listRef.elementCount() *
                    (ONE * POINTERS / ELEMENTS) * WORDS_PER_POINTER,
                    WirePointer::LIST, nullptr));

            for (auto i: kj::zeroTo(src->listRef.elementCount() * (ONE * POINTERS / ELEMENTS))) {
              SegmentBuilder* subSegment = segment;
              WirePointer* dstRef = dstRefs + i;
              copyMessage(subSegment, dstRef, srcRefs + i);
            }

            dst->listRef.set(ElementSize::POINTER, src->listRef.elementCount());
            return reinterpret_cast<word*>(dstRefs);
          }

          case ElementSize::INLINE_COMPOSITE: {
            const word* srcPtr = src->target();
            word* dstPtr = allocate(dst, segment,
                assertMaxBits<SEGMENT_WORD_COUNT_BITS>(
                    src->listRef.inlineCompositeWordCount() + POINTER_SIZE_IN_WORDS,
                    []() { KJ_FAIL_ASSERT("list too big to fit in a segment"); }),
                WirePointer::LIST, nullptr);

            dst->listRef.setInlineComposite(src->listRef.inlineCompositeWordCount());

            const WirePointer* srcTag = reinterpret_cast<const WirePointer*>(srcPtr);
            memcpy(dstPtr, srcTag, sizeof(WirePointer));

            const word* srcElement = srcPtr + POINTER_SIZE_IN_WORDS;
            word* dstElement = dstPtr + POINTER_SIZE_IN_WORDS;

            KJ_ASSERT(srcTag->kind() == WirePointer::STRUCT,
                "INLINE_COMPOSITE of lists is not yet supported.");

            for (auto i KJ_UNUSED: kj::zeroTo(srcTag->inlineCompositeListElementCount())) {
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
      case WirePointer::OTHER:
        KJ_FAIL_REQUIRE("Unchecked messages cannot contain OTHER pointers (e.g. capabilities).");
        break;
      case WirePointer::FAR:
        KJ_FAIL_REQUIRE("Unchecked messages cannot contain far pointers.");
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
      dst->setKindAndTarget(srcTag->kind(), srcPtr, dstSegment);

      // We can just copy the upper 32 bits.  (Use memcpy() to comply with aliasing rules.)
      memcpy(&dst->upper32Bits, &srcTag->upper32Bits, sizeof(srcTag->upper32Bits));
    } else {
      // Need to create a far pointer.  Try to allocate it in the same segment as the source, so
      // that it doesn't need to be a double-far.

      WirePointer* landingPad =
          reinterpret_cast<WirePointer*>(srcSegment->allocate(G(1) * WORDS));
      if (landingPad == nullptr) {
        // Darn, need a double-far.
        auto allocation = srcSegment->getArena()->allocate(G(2) * WORDS);
        SegmentBuilder* farSegment = allocation.segment;
        landingPad = reinterpret_cast<WirePointer*>(allocation.words);

        landingPad[0].setFar(false, srcSegment->getOffsetTo(srcPtr));
        landingPad[0].farRef.segmentId.set(srcSegment->getSegmentId());

        landingPad[1].setKindWithZeroOffset(srcTag->kind());
        memcpy(&landingPad[1].upper32Bits, &srcTag->upper32Bits, sizeof(srcTag->upper32Bits));

        dst->setFar(true, farSegment->getOffsetTo(reinterpret_cast<word*>(landingPad)));
        dst->farRef.set(farSegment->getSegmentId());
      } else {
        // Simple landing pad is just a pointer.
        landingPad->setKindAndTarget(srcTag->kind(), srcPtr, srcSegment);
        memcpy(&landingPad->upper32Bits, &srcTag->upper32Bits, sizeof(srcTag->upper32Bits));

        dst->setFar(false, srcSegment->getOffsetTo(reinterpret_cast<word*>(landingPad)));
        dst->farRef.set(srcSegment->getSegmentId());
      }
    }
  }

  // -----------------------------------------------------------------

  static KJ_ALWAYS_INLINE(StructBuilder initStructPointer(
      WirePointer* ref, SegmentBuilder* segment, StructSize size,
      BuilderArena* orphanArena = nullptr)) {
    // Allocate space for the new struct.  Newly-allocated space is automatically zeroed.
    word* ptr = allocate(ref, segment, size.total(), WirePointer::STRUCT, orphanArena);

    // Initialize the pointer.
    ref->structRef.set(size);

    // Build the StructBuilder.
    return StructBuilder(segment, ptr, reinterpret_cast<WirePointer*>(ptr + size.data),
                         size.data * BITS_PER_WORD, size.pointers);
  }

  static KJ_ALWAYS_INLINE(StructBuilder getWritableStructPointer(
      WirePointer* ref, SegmentBuilder* segment, StructSize size, const word* defaultValue)) {
    return getWritableStructPointer(ref, ref->target(), segment, size, defaultValue);
  }

  static KJ_ALWAYS_INLINE(StructBuilder getWritableStructPointer(
      WirePointer* ref, word* refTarget, SegmentBuilder* segment, StructSize size,
      const word* defaultValue, BuilderArena* orphanArena = nullptr)) {
    if (ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return initStructPointer(ref, segment, size, orphanArena);
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

    auto oldDataSize = oldRef->structRef.dataSize.get();
    auto oldPointerCount = oldRef->structRef.ptrCount.get();
    WirePointer* oldPointerSection =
        reinterpret_cast<WirePointer*>(oldPtr + oldDataSize);

    if (oldDataSize < size.data || oldPointerCount < size.pointers) {
      // The space allocated for this struct is too small.  Unlike with readers, we can't just
      // run with it and do bounds checks at access time, because how would we handle writes?
      // Instead, we have to copy the struct to a new space now.

      auto newDataSize = kj::max(oldDataSize, size.data);
      auto newPointerCount = kj::max(oldPointerCount, size.pointers);
      auto totalSize = newDataSize + newPointerCount * WORDS_PER_POINTER;

      // Don't let allocate() zero out the object just yet.
      zeroPointerAndFars(segment, ref);

      word* ptr = allocate(ref, segment, totalSize, WirePointer::STRUCT, orphanArena);
      ref->structRef.set(newDataSize, newPointerCount);

      // Copy data section.
      copyMemory(ptr, oldPtr, oldDataSize);

      // Copy pointer section.
      WirePointer* newPointerSection = reinterpret_cast<WirePointer*>(ptr + newDataSize);
      for (auto i: kj::zeroTo(oldPointerCount)) {
        transferPointer(segment, newPointerSection + i, oldSegment, oldPointerSection + i);
      }

      // Zero out old location.  This has two purposes:
      // 1) We don't want to leak the original contents of the struct when the message is written
      //    out as it may contain secrets that the caller intends to remove from the new copy.
      // 2) Zeros will be deflated by packing, making this dead memory almost-free if it ever
      //    hits the wire.
      zeroMemory(oldPtr, oldDataSize + oldPointerCount * WORDS_PER_POINTER);

      return StructBuilder(segment, ptr, newPointerSection, newDataSize * BITS_PER_WORD,
                           newPointerCount);
    } else {
      return StructBuilder(oldSegment, oldPtr, oldPointerSection, oldDataSize * BITS_PER_WORD,
                           oldPointerCount);
    }
  }

  static KJ_ALWAYS_INLINE(ListBuilder initListPointer(
      WirePointer* ref, SegmentBuilder* segment, ElementCount elementCount,
      ElementSize elementSize, BuilderArena* orphanArena = nullptr)) {
    KJ_DREQUIRE(elementSize != ElementSize::INLINE_COMPOSITE,
        "Should have called initStructListPointer() instead.");

    auto checkedElementCount = assertMaxBits<LIST_ELEMENT_COUNT_BITS>(elementCount,
        []() { KJ_FAIL_REQUIRE("tried to allocate list with too many elements"); });

    auto dataSize = dataBitsPerElement(elementSize) * ELEMENTS;
    auto pointerCount = pointersPerElement(elementSize) * ELEMENTS;
    auto step = bitsPerElementIncludingPointers(elementSize);
    KJ_DASSERT(step * ELEMENTS == (dataSize + pointerCount * BITS_PER_POINTER));

    // Calculate size of the list.
    auto wordCount = roundBitsUpToWords(upgradeGuard<uint64_t>(checkedElementCount) * step);

    // Allocate the list.
    word* ptr = allocate(ref, segment, wordCount, WirePointer::LIST, orphanArena);

    // Initialize the pointer.
    ref->listRef.set(elementSize, checkedElementCount);

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, step, checkedElementCount,
                       dataSize, pointerCount, elementSize);
  }

  static KJ_ALWAYS_INLINE(ListBuilder initStructListPointer(
      WirePointer* ref, SegmentBuilder* segment, ElementCount elementCount,
      StructSize elementSize, BuilderArena* orphanArena = nullptr)) {
    auto checkedElementCount = assertMaxBits<LIST_ELEMENT_COUNT_BITS>(elementCount,
        []() { KJ_FAIL_REQUIRE("tried to allocate list with too many elements"); });

    WordsPerElementN<17> wordsPerElement = elementSize.total() / ELEMENTS;

    // Allocate the list, prefixed by a single WirePointer.
    auto wordCount = assertMax<kj::maxValueForBits<SEGMENT_WORD_COUNT_BITS>() - 1>(
        upgradeGuard<uint64_t>(checkedElementCount) * wordsPerElement,
        []() { KJ_FAIL_REQUIRE("total size of struct list is larger than max segment size"); });
    word* ptr = allocate(ref, segment, POINTER_SIZE_IN_WORDS + wordCount, WirePointer::LIST,
                         orphanArena);

    // Initialize the pointer.
    // INLINE_COMPOSITE lists replace the element count with the word count.
    ref->listRef.setInlineComposite(wordCount);

    // Initialize the list tag.
    reinterpret_cast<WirePointer*>(ptr)->setKindAndInlineCompositeListElementCount(
        WirePointer::STRUCT, checkedElementCount);
    reinterpret_cast<WirePointer*>(ptr)->structRef.set(elementSize);
    ptr += POINTER_SIZE_IN_WORDS;

    // Build the ListBuilder.
    return ListBuilder(segment, ptr, wordsPerElement * BITS_PER_WORD, checkedElementCount,
                       elementSize.data * BITS_PER_WORD, elementSize.pointers,
                       ElementSize::INLINE_COMPOSITE);
  }

  static KJ_ALWAYS_INLINE(ListBuilder getWritableListPointer(
      WirePointer* origRef, SegmentBuilder* origSegment, ElementSize elementSize,
      const word* defaultValue)) {
    return getWritableListPointer(origRef, origRef->target(), origSegment, elementSize,
                                  defaultValue);
  }

  static KJ_ALWAYS_INLINE(ListBuilder getWritableListPointer(
      WirePointer* origRef, word* origRefTarget, SegmentBuilder* origSegment, ElementSize elementSize,
      const word* defaultValue, BuilderArena* orphanArena = nullptr)) {
    KJ_DREQUIRE(elementSize != ElementSize::INLINE_COMPOSITE,
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
    // getWritableStructListPointer(), we never need to "upgrade" the data, because this
    // method is called only for non-struct lists, and there is no allowed upgrade path *to*
    // a non-struct list, only *from* them.

    WirePointer* ref = origRef;
    SegmentBuilder* segment = origSegment;
    word* ptr = followFars(ref, origRefTarget, segment);

    KJ_REQUIRE(ref->kind() == WirePointer::LIST,
        "Called getList{Field,Element}() but existing pointer is not a list.") {
      goto useDefault;
    }

    ElementSize oldSize = ref->listRef.elementSize();

    if (oldSize == ElementSize::INLINE_COMPOSITE) {
      // The existing element size is INLINE_COMPOSITE, though we expected a list of primitives.
      // The existing data must have been written with a newer version of the protocol.  We
      // therefore never need to upgrade the data in this case, but we do need to validate that it
      // is a valid upgrade from what we expected.

      // Read the tag to get the actual element count.
      WirePointer* tag = reinterpret_cast<WirePointer*>(ptr);
      KJ_REQUIRE(tag->kind() == WirePointer::STRUCT,
          "INLINE_COMPOSITE list with non-STRUCT elements not supported.");
      ptr += POINTER_SIZE_IN_WORDS;

      auto dataSize = tag->structRef.dataSize.get();
      auto pointerCount = tag->structRef.ptrCount.get();

      switch (elementSize) {
        case ElementSize::VOID:
          // Anything is a valid upgrade from Void.
          break;

        case ElementSize::BIT:
          KJ_FAIL_REQUIRE(
              "Found struct list where bit list was expected; upgrading boolean lists to structs "
              "is no longer supported.") {
            goto useDefault;
          }
          break;

        case ElementSize::BYTE:
        case ElementSize::TWO_BYTES:
        case ElementSize::FOUR_BYTES:
        case ElementSize::EIGHT_BYTES:
          KJ_REQUIRE(dataSize >= ONE * WORDS,
                     "Existing list value is incompatible with expected type.") {
            goto useDefault;
          }
          break;

        case ElementSize::POINTER:
          KJ_REQUIRE(pointerCount >= ONE * POINTERS,
                     "Existing list value is incompatible with expected type.") {
            goto useDefault;
          }
          // Adjust the pointer to point at the reference segment.
          ptr += dataSize;
          break;

        case ElementSize::INLINE_COMPOSITE:
          KJ_UNREACHABLE;
      }

      // OK, looks valid.

      return ListBuilder(segment, ptr,
                         tag->structRef.wordSize() * BITS_PER_WORD / ELEMENTS,
                         tag->inlineCompositeListElementCount(),
                         dataSize * BITS_PER_WORD, pointerCount, ElementSize::INLINE_COMPOSITE);
    } else {
      auto dataSize = dataBitsPerElement(oldSize) * ELEMENTS;
      auto pointerCount = pointersPerElement(oldSize) * ELEMENTS;

      if (elementSize == ElementSize::BIT) {
        KJ_REQUIRE(oldSize == ElementSize::BIT,
            "Found non-bit list where bit list was expected.") {
          goto useDefault;
        }
      } else {
        KJ_REQUIRE(oldSize != ElementSize::BIT,
            "Found bit list where non-bit list was expected.") {
          goto useDefault;
        }
        KJ_REQUIRE(dataSize >= dataBitsPerElement(elementSize) * ELEMENTS,
                   "Existing list value is incompatible with expected type.") {
          goto useDefault;
        }
        KJ_REQUIRE(pointerCount >= pointersPerElement(elementSize) * ELEMENTS,
                   "Existing list value is incompatible with expected type.") {
          goto useDefault;
        }
      }

      auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;
      return ListBuilder(segment, ptr, step, ref->listRef.elementCount(),
                         dataSize, pointerCount, oldSize);
    }
  }

  static KJ_ALWAYS_INLINE(ListBuilder getWritableListPointerAnySize(
      WirePointer* origRef, SegmentBuilder* origSegment, const word* defaultValue)) {
    return getWritableListPointerAnySize(origRef, origRef->target(), origSegment, defaultValue);
  }

  static KJ_ALWAYS_INLINE(ListBuilder getWritableListPointerAnySize(
      WirePointer* origRef, word* origRefTarget, SegmentBuilder* origSegment,
      const word* defaultValue, BuilderArena* orphanArena = nullptr)) {
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

    WirePointer* ref = origRef;
    SegmentBuilder* segment = origSegment;
    word* ptr = followFars(ref, origRefTarget, segment);

    KJ_REQUIRE(ref->kind() == WirePointer::LIST,
        "Called getList{Field,Element}() but existing pointer is not a list.") {
      goto useDefault;
    }

    ElementSize elementSize = ref->listRef.elementSize();

    if (elementSize == ElementSize::INLINE_COMPOSITE) {
      // Read the tag to get the actual element count.
      WirePointer* tag = reinterpret_cast<WirePointer*>(ptr);
      KJ_REQUIRE(tag->kind() == WirePointer::STRUCT,
          "INLINE_COMPOSITE list with non-STRUCT elements not supported.");
      ptr += POINTER_SIZE_IN_WORDS;

      return ListBuilder(segment, ptr,
                         tag->structRef.wordSize() * BITS_PER_WORD / ELEMENTS,
                         tag->inlineCompositeListElementCount(),
                         tag->structRef.dataSize.get() * BITS_PER_WORD,
                         tag->structRef.ptrCount.get(), ElementSize::INLINE_COMPOSITE);
    } else {
      auto dataSize = dataBitsPerElement(elementSize) * ELEMENTS;
      auto pointerCount = pointersPerElement(elementSize) * ELEMENTS;

      auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;
      return ListBuilder(segment, ptr, step, ref->listRef.elementCount(),
                         dataSize, pointerCount, elementSize);
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
      StructSize elementSize, const word* defaultValue, BuilderArena* orphanArena = nullptr)) {
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

    ElementSize oldSize = oldRef->listRef.elementSize();

    if (oldSize == ElementSize::INLINE_COMPOSITE) {
      // Existing list is INLINE_COMPOSITE, but we need to verify that the sizes match.

      WirePointer* oldTag = reinterpret_cast<WirePointer*>(oldPtr);
      oldPtr += POINTER_SIZE_IN_WORDS;
      KJ_REQUIRE(oldTag->kind() == WirePointer::STRUCT,
                 "INLINE_COMPOSITE list with non-STRUCT elements not supported.") {
        goto useDefault;
      }

      auto oldDataSize = oldTag->structRef.dataSize.get();
      auto oldPointerCount = oldTag->structRef.ptrCount.get();
      auto oldStep = (oldDataSize + oldPointerCount * WORDS_PER_POINTER) / ELEMENTS;

      auto elementCount = oldTag->inlineCompositeListElementCount();

      if (oldDataSize >= elementSize.data && oldPointerCount >= elementSize.pointers) {
        // Old size is at least as large as we need.  Ship it.
        return ListBuilder(oldSegment, oldPtr, oldStep * BITS_PER_WORD, elementCount,
                           oldDataSize * BITS_PER_WORD, oldPointerCount,
                           ElementSize::INLINE_COMPOSITE);
      }

      // The structs in this list are smaller than expected, probably written using an older
      // version of the protocol.  We need to make a copy and expand them.

      auto newDataSize = kj::max(oldDataSize, elementSize.data);
      auto newPointerCount = kj::max(oldPointerCount, elementSize.pointers);
      auto newStep = (newDataSize + newPointerCount * WORDS_PER_POINTER) / ELEMENTS;

      auto totalSize = assertMax<kj::maxValueForBits<SEGMENT_WORD_COUNT_BITS>() - 1>(
            newStep * upgradeGuard<uint64_t>(elementCount),
            []() { KJ_FAIL_REQUIRE("total size of struct list is larger than max segment size"); });

      // Don't let allocate() zero out the object just yet.
      zeroPointerAndFars(origSegment, origRef);

      word* newPtr = allocate(origRef, origSegment, totalSize + POINTER_SIZE_IN_WORDS,
                              WirePointer::LIST, orphanArena);
      origRef->listRef.setInlineComposite(totalSize);

      WirePointer* newTag = reinterpret_cast<WirePointer*>(newPtr);
      newTag->setKindAndInlineCompositeListElementCount(WirePointer::STRUCT, elementCount);
      newTag->structRef.set(newDataSize, newPointerCount);
      newPtr += POINTER_SIZE_IN_WORDS;

      word* src = oldPtr;
      word* dst = newPtr;
      for (auto i KJ_UNUSED: kj::zeroTo(elementCount)) {
        // Copy data section.
        copyMemory(dst, src, oldDataSize);

        // Copy pointer section.
        WirePointer* newPointerSection = reinterpret_cast<WirePointer*>(dst + newDataSize);
        WirePointer* oldPointerSection = reinterpret_cast<WirePointer*>(src + oldDataSize);
        for (auto j: kj::zeroTo(oldPointerCount)) {
          transferPointer(origSegment, newPointerSection + j, oldSegment, oldPointerSection + j);
        }

        dst += newStep * (ONE * ELEMENTS);
        src += oldStep * (ONE * ELEMENTS);
      }

      auto oldSize = assertMax<kj::maxValueForBits<SEGMENT_WORD_COUNT_BITS>() - 1>(
            oldStep * upgradeGuard<uint64_t>(elementCount),
            []() { KJ_FAIL_ASSERT("old size overflows but new size doesn't?"); });

      // Zero out old location.  See explanation in getWritableStructPointer().
      zeroMemory(oldPtr, oldSize);

      return ListBuilder(origSegment, newPtr, newStep * BITS_PER_WORD, elementCount,
                         newDataSize * BITS_PER_WORD, newPointerCount,
                         ElementSize::INLINE_COMPOSITE);
    } else {
      // We're upgrading from a non-struct list.

      auto oldDataSize = dataBitsPerElement(oldSize) * ELEMENTS;
      auto oldPointerCount = pointersPerElement(oldSize) * ELEMENTS;
      auto oldStep = (oldDataSize + oldPointerCount * BITS_PER_POINTER) / ELEMENTS;
      auto elementCount = oldRef->listRef.elementCount();

      if (oldSize == ElementSize::VOID) {
        // Nothing to copy, just allocate a new list.
        return initStructListPointer(origRef, origSegment, elementCount, elementSize);
      } else {
        // Upgrading to an inline composite list.

        KJ_REQUIRE(oldSize != ElementSize::BIT,
            "Found bit list where struct list was expected; upgrading boolean lists to structs "
            "is no longer supported.") {
          goto useDefault;
        }

        auto newDataSize = elementSize.data;
        auto newPointerCount = elementSize.pointers;

        if (oldSize == ElementSize::POINTER) {
          newPointerCount = kj::max(newPointerCount, ONE * POINTERS);
        } else {
          // Old list contains data elements, so we need at least 1 word of data.
          newDataSize = kj::max(newDataSize, ONE * WORDS);
        }

        auto newStep = (newDataSize + newPointerCount * WORDS_PER_POINTER) / ELEMENTS;
        auto totalWords = assertMax<kj::maxValueForBits<SEGMENT_WORD_COUNT_BITS>() - 1>(
              newStep * upgradeGuard<uint64_t>(elementCount),
              []() {KJ_FAIL_REQUIRE("total size of struct list is larger than max segment size");});

        // Don't let allocate() zero out the object just yet.
        zeroPointerAndFars(origSegment, origRef);

        word* newPtr = allocate(origRef, origSegment, totalWords + POINTER_SIZE_IN_WORDS,
                                WirePointer::LIST, orphanArena);
        origRef->listRef.setInlineComposite(totalWords);

        WirePointer* tag = reinterpret_cast<WirePointer*>(newPtr);
        tag->setKindAndInlineCompositeListElementCount(WirePointer::STRUCT, elementCount);
        tag->structRef.set(newDataSize, newPointerCount);
        newPtr += POINTER_SIZE_IN_WORDS;

        if (oldSize == ElementSize::POINTER) {
          WirePointer* dst = reinterpret_cast<WirePointer*>(newPtr + newDataSize);
          WirePointer* src = reinterpret_cast<WirePointer*>(oldPtr);
          for (auto i KJ_UNUSED: kj::zeroTo(elementCount)) {
            transferPointer(origSegment, dst, oldSegment, src);
            dst += newStep / WORDS_PER_POINTER * (ONE * ELEMENTS);
            ++src;
          }
        } else {
          byte* dst = reinterpret_cast<byte*>(newPtr);
          byte* src = reinterpret_cast<byte*>(oldPtr);
          auto newByteStep = newStep * (ONE * ELEMENTS) * BYTES_PER_WORD;
          auto oldByteStep = oldDataSize / BITS_PER_BYTE;
          for (auto i KJ_UNUSED: kj::zeroTo(elementCount)) {
            copyMemory(dst, src, oldByteStep);
            src += oldByteStep;
            dst += newByteStep;
          }
        }

        auto oldSize = assertMax<kj::maxValueForBits<SEGMENT_WORD_COUNT_BITS>() - 1>(
              roundBitsUpToWords(oldStep * upgradeGuard<uint64_t>(elementCount)),
              []() { KJ_FAIL_ASSERT("old size overflows but new size doesn't?"); });

        // Zero out old location.  See explanation in getWritableStructPointer().
        zeroMemory(oldPtr, oldSize);

        return ListBuilder(origSegment, newPtr, newStep * BITS_PER_WORD, elementCount,
                           newDataSize * BITS_PER_WORD, newPointerCount,
                           ElementSize::INLINE_COMPOSITE);
      }
    }
  }

  static KJ_ALWAYS_INLINE(SegmentAnd<Text::Builder> initTextPointer(
      WirePointer* ref, SegmentBuilder* segment, TextSize size,
      BuilderArena* orphanArena = nullptr)) {
    // The byte list must include a NUL terminator.
    auto byteSize = size + ONE * BYTES;

    // Allocate the space.
    word* ptr = allocate(
        ref, segment, roundBytesUpToWords(byteSize), WirePointer::LIST, orphanArena);

    // Initialize the pointer.
    ref->listRef.set(ElementSize::BYTE, byteSize * (ONE * ELEMENTS / BYTES));

    // Build the Text::Builder.  This will initialize the NUL terminator.
    return { segment, Text::Builder(reinterpret_cast<char*>(ptr), unguard(size / BYTES)) };
  }

  static KJ_ALWAYS_INLINE(SegmentAnd<Text::Builder> setTextPointer(
      WirePointer* ref, SegmentBuilder* segment, Text::Reader value,
      BuilderArena* orphanArena = nullptr)) {
    TextSize size = assertMax<MAX_TEXT_SIZE>(guarded(value.size()),
        []() { KJ_FAIL_REQUIRE("text blob too big"); }) * BYTES;

    auto allocation = initTextPointer(ref, segment, size, orphanArena);
    memcpy(allocation.value.begin(), value.begin(), value.size());
    return allocation;
  }

  static KJ_ALWAYS_INLINE(Text::Builder getWritableTextPointer(
      WirePointer* ref, SegmentBuilder* segment,
      const void* defaultValue, TextSize defaultSize)) {
    return getWritableTextPointer(ref, ref->target(), segment, defaultValue, defaultSize);
  }

  static KJ_ALWAYS_INLINE(Text::Builder getWritableTextPointer(
      WirePointer* ref, word* refTarget, SegmentBuilder* segment,
      const void* defaultValue, TextSize defaultSize)) {
    if (ref->isNull()) {
    useDefault:
      if (defaultSize == ZERO * BYTES) {
        return nullptr;
      } else {
        Text::Builder builder = initTextPointer(ref, segment, defaultSize).value;
        copyMemory(builder.asBytes().begin(), reinterpret_cast<const byte*>(defaultValue),
                   defaultSize);
        return builder;
      }
    } else {
      word* ptr = followFars(ref, refTarget, segment);
      char* cptr = reinterpret_cast<char*>(ptr);

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
          "Called getText{Field,Element}() but existing pointer is not a list.");
      KJ_REQUIRE(ref->listRef.elementSize() == ElementSize::BYTE,
          "Called getText{Field,Element}() but existing list pointer is not byte-sized.");

      size_t size = unguard(subtractChecked(ref->listRef.elementCount() / ELEMENTS, ONE,
          []() { KJ_FAIL_REQUIRE("zero-size blob can't be text (need NUL terminator)"); }));
      KJ_REQUIRE(cptr[size] == '\0', "Text blob missing NUL terminator.") {
        goto useDefault;
      }

      return Text::Builder(cptr, size);
    }
  }

  static KJ_ALWAYS_INLINE(SegmentAnd<Data::Builder> initDataPointer(
      WirePointer* ref, SegmentBuilder* segment, BlobSize size,
      BuilderArena* orphanArena = nullptr)) {
    // Allocate the space.
    word* ptr = allocate(ref, segment, roundBytesUpToWords(size), WirePointer::LIST, orphanArena);

    // Initialize the pointer.
    ref->listRef.set(ElementSize::BYTE, size * (ONE * ELEMENTS / BYTES));

    // Build the Data::Builder.
    return { segment, Data::Builder(reinterpret_cast<byte*>(ptr), unguard(size / BYTES)) };
  }

  static KJ_ALWAYS_INLINE(SegmentAnd<Data::Builder> setDataPointer(
      WirePointer* ref, SegmentBuilder* segment, Data::Reader value,
      BuilderArena* orphanArena = nullptr)) {
    BlobSize size = assertMaxBits<BLOB_SIZE_BITS>(guarded(value.size()),
        []() { KJ_FAIL_REQUIRE("text blob too big"); }) * BYTES;

    auto allocation = initDataPointer(ref, segment, size, orphanArena);
    memcpy(allocation.value.begin(), value.begin(), value.size());
    return allocation;
  }

  static KJ_ALWAYS_INLINE(Data::Builder getWritableDataPointer(
      WirePointer* ref, SegmentBuilder* segment,
      const void* defaultValue, BlobSize defaultSize)) {
    return getWritableDataPointer(ref, ref->target(), segment, defaultValue, defaultSize);
  }

  static KJ_ALWAYS_INLINE(Data::Builder getWritableDataPointer(
      WirePointer* ref, word* refTarget, SegmentBuilder* segment,
      const void* defaultValue, BlobSize defaultSize)) {
    if (ref->isNull()) {
      if (defaultSize == ZERO * BYTES) {
        return nullptr;
      } else {
        Data::Builder builder = initDataPointer(ref, segment, defaultSize).value;
        copyMemory(builder.begin(), reinterpret_cast<const byte*>(defaultValue), defaultSize);
        return builder;
      }
    } else {
      word* ptr = followFars(ref, refTarget, segment);

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
          "Called getData{Field,Element}() but existing pointer is not a list.");
      KJ_REQUIRE(ref->listRef.elementSize() == ElementSize::BYTE,
          "Called getData{Field,Element}() but existing list pointer is not byte-sized.");

      return Data::Builder(reinterpret_cast<byte*>(ptr),
          unguard(ref->listRef.elementCount() / ELEMENTS));
    }
  }

  static SegmentAnd<word*> setStructPointer(
      SegmentBuilder* segment, WirePointer* ref, StructReader value,
      BuilderArena* orphanArena = nullptr) {
    auto dataSize = roundBitsUpToWords(value.dataSize);
    auto totalSize = dataSize + value.pointerCount * WORDS_PER_POINTER;

    word* ptr = allocate(ref, segment, totalSize, WirePointer::STRUCT, orphanArena);
    ref->structRef.set(dataSize, value.pointerCount);

    if (value.dataSize == ONE * BITS) {
      *reinterpret_cast<char*>(ptr) = value.getDataField<bool>(ZERO * ELEMENTS);
    } else {
      copyMemory(reinterpret_cast<byte*>(ptr),
                 reinterpret_cast<const byte*>(value.data),
                 value.dataSize / BITS_PER_BYTE);
    }

    WirePointer* pointerSection = reinterpret_cast<WirePointer*>(ptr + dataSize);
    for (auto i: kj::zeroTo(value.pointerCount)) {
      copyPointer(segment, pointerSection + i, value.segment, value.pointers + i,
                  value.nestingLimit);
    }

    return { segment, ptr };
  }

#if !CAPNP_LITE
  static void setCapabilityPointer(
      SegmentBuilder* segment, WirePointer* ref, kj::Own<ClientHook>&& cap,
      BuilderArena* orphanArena = nullptr) {
    if (orphanArena == nullptr) {
      ref->setCap(segment->getArena()->injectCap(kj::mv(cap)));
    } else {
      ref->setCap(orphanArena->injectCap(kj::mv(cap)));
    }
  }
#endif  // !CAPNP_LITE

  static SegmentAnd<word*> setListPointer(
      SegmentBuilder* segment, WirePointer* ref, ListReader value,
      BuilderArena* orphanArena = nullptr) {
    auto totalSize = assertMax<kj::maxValueForBits<SEGMENT_WORD_COUNT_BITS>() - 1>(
        roundBitsUpToWords(upgradeGuard<uint64_t>(value.elementCount) * value.step),
        []() { KJ_FAIL_ASSERT("encountered impossibly long struct list ListReader"); });

    if (value.elementSize != ElementSize::INLINE_COMPOSITE) {
      // List of non-structs.
      word* ptr = allocate(ref, segment, totalSize, WirePointer::LIST, orphanArena);

      if (value.elementSize == ElementSize::POINTER) {
        // List of pointers.
        ref->listRef.set(ElementSize::POINTER, value.elementCount);
        for (auto i: zeroTo(value.elementCount * (ONE * POINTERS / ELEMENTS))) {
          copyPointer(segment, reinterpret_cast<WirePointer*>(ptr) + i,
                      value.segment, reinterpret_cast<const WirePointer*>(value.ptr) + i,
                      value.nestingLimit);
        }
      } else {
        // List of data.
        ref->listRef.set(value.elementSize, value.elementCount);
        copyMemory(ptr, reinterpret_cast<const word*>(value.ptr), totalSize);
      }

      return { segment, ptr };
    } else {
      // List of structs.
      word* ptr = allocate(ref, segment, totalSize + POINTER_SIZE_IN_WORDS, WirePointer::LIST,
                           orphanArena);
      ref->listRef.setInlineComposite(totalSize);

      auto dataSize = roundBitsUpToWords(value.structDataSize);
      auto pointerCount = value.structPointerCount;

      WirePointer* tag = reinterpret_cast<WirePointer*>(ptr);
      tag->setKindAndInlineCompositeListElementCount(WirePointer::STRUCT, value.elementCount);
      tag->structRef.set(dataSize, pointerCount);
      word* dst = ptr + POINTER_SIZE_IN_WORDS;

      const word* src = reinterpret_cast<const word*>(value.ptr);
      for (auto i KJ_UNUSED: kj::zeroTo(value.elementCount)) {
        copyMemory(dst, src, value.structDataSize / BITS_PER_WORD);
        dst += dataSize;
        src += dataSize;

        for (auto j KJ_UNUSED: kj::zeroTo(pointerCount)) {
          copyPointer(segment, reinterpret_cast<WirePointer*>(dst),
              value.segment, reinterpret_cast<const WirePointer*>(src), value.nestingLimit);
          dst += POINTER_SIZE_IN_WORDS;
          src += POINTER_SIZE_IN_WORDS;
        }
      }

      return { segment, ptr };
    }
  }

  static KJ_ALWAYS_INLINE(SegmentAnd<word*> copyPointer(
      SegmentBuilder* dstSegment, WirePointer* dst,
      SegmentReader* srcSegment, const WirePointer* src,
      int nestingLimit, BuilderArena* orphanArena = nullptr)) {
    return copyPointer(dstSegment, dst, srcSegment, src, src->target(), nestingLimit, orphanArena);
  }

  static SegmentAnd<word*> copyPointer(
      SegmentBuilder* dstSegment, WirePointer* dst,
      SegmentReader* srcSegment, const WirePointer* src, const word* srcTarget,
      int nestingLimit, BuilderArena* orphanArena = nullptr) {
    // Deep-copy the object pointed to by src into dst.  It turns out we can't reuse
    // readStructPointer(), etc. because they do type checking whereas here we want to accept any
    // valid pointer.

    if (src->isNull()) {
    useDefault:
      memset(dst, 0, sizeof(*dst));
      return { dstSegment, nullptr };
    }

    const word* ptr = WireHelpers::followFars(src, srcTarget, srcSegment);
    if (KJ_UNLIKELY(ptr == nullptr)) {
      // Already reported the error.
      goto useDefault;
    }

    switch (src->kind()) {
      case WirePointer::STRUCT:
        KJ_REQUIRE(nestingLimit > 0,
              "Message is too deeply-nested or contains cycles.  See capnp::ReaderOptions.") {
          goto useDefault;
        }

        KJ_REQUIRE(boundsCheck(srcSegment, ptr, ptr + src->structRef.wordSize()),
                   "Message contained out-of-bounds struct pointer.") {
          goto useDefault;
        }
        return setStructPointer(dstSegment, dst,
            StructReader(srcSegment, ptr,
                         reinterpret_cast<const WirePointer*>(ptr + src->structRef.dataSize.get()),
                         src->structRef.dataSize.get() * BITS_PER_WORD,
                         src->structRef.ptrCount.get(),
                         nestingLimit - 1),
            orphanArena);

      case WirePointer::LIST: {
        ElementSize elementSize = src->listRef.elementSize();

        KJ_REQUIRE(nestingLimit > 0,
              "Message is too deeply-nested or contains cycles.  See capnp::ReaderOptions.") {
          goto useDefault;
        }

        if (elementSize == ElementSize::INLINE_COMPOSITE) {
          auto wordCount = src->listRef.inlineCompositeWordCount();
          const WirePointer* tag = reinterpret_cast<const WirePointer*>(ptr);
          ptr += POINTER_SIZE_IN_WORDS;

          KJ_REQUIRE(boundsCheck(srcSegment, ptr - POINTER_SIZE_IN_WORDS, ptr + wordCount),
                     "Message contains out-of-bounds list pointer.") {
            goto useDefault;
          }

          KJ_REQUIRE(tag->kind() == WirePointer::STRUCT,
                     "INLINE_COMPOSITE lists of non-STRUCT type are not supported.") {
            goto useDefault;
          }

          auto elementCount = tag->inlineCompositeListElementCount();
          auto wordsPerElement = tag->structRef.wordSize() / ELEMENTS;

          KJ_REQUIRE(wordsPerElement * upgradeGuard<uint64_t>(elementCount) <= wordCount,
                     "INLINE_COMPOSITE list's elements overrun its word count.") {
            goto useDefault;
          }

          if (wordsPerElement * (1 * ELEMENTS) == 0 * WORDS) {
            // Watch out for lists of zero-sized structs, which can claim to be arbitrarily large
            // without having sent actual data.
            KJ_REQUIRE(amplifiedRead(srcSegment, elementCount * (1 * WORDS / ELEMENTS)),
                       "Message contains amplified list pointer.") {
              goto useDefault;
            }
          }

          return setListPointer(dstSegment, dst,
              ListReader(srcSegment, ptr, elementCount, wordsPerElement * BITS_PER_WORD,
                         tag->structRef.dataSize.get() * BITS_PER_WORD,
                         tag->structRef.ptrCount.get(), ElementSize::INLINE_COMPOSITE,
                         nestingLimit - 1),
              orphanArena);
        } else {
          auto dataSize = dataBitsPerElement(elementSize) * ELEMENTS;
          auto pointerCount = pointersPerElement(elementSize) * ELEMENTS;
          auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;
          auto elementCount = src->listRef.elementCount();
          auto wordCount = roundBitsUpToWords(upgradeGuard<uint64_t>(elementCount) * step);

          KJ_REQUIRE(boundsCheck(srcSegment, ptr, ptr + wordCount),
                     "Message contains out-of-bounds list pointer.") {
            goto useDefault;
          }

          if (elementSize == ElementSize::VOID) {
            // Watch out for lists of void, which can claim to be arbitrarily large without having
            // sent actual data.
            KJ_REQUIRE(amplifiedRead(srcSegment, elementCount * (1 * WORDS / ELEMENTS)),
                       "Message contains amplified list pointer.") {
              goto useDefault;
            }
          }

          return setListPointer(dstSegment, dst,
              ListReader(srcSegment, ptr, elementCount, step, dataSize, pointerCount, elementSize,
                         nestingLimit - 1),
              orphanArena);
        }
      }

      case WirePointer::FAR:
        KJ_FAIL_ASSERT("Far pointer should have been handled above.") {
          goto useDefault;
        }

      case WirePointer::OTHER: {
        KJ_REQUIRE(src->isCapability(), "Unknown pointer type.") {
          goto useDefault;
        }

#if !CAPNP_LITE
        KJ_IF_MAYBE(cap, srcSegment->getArena()->extractCap(src->capRef.index.get())) {
          setCapabilityPointer(dstSegment, dst, kj::mv(*cap), orphanArena);
          return { dstSegment, nullptr };
        } else {
#endif  // !CAPNP_LITE
          KJ_FAIL_REQUIRE("Message contained invalid capability pointer.") {
            goto useDefault;
          }
#if !CAPNP_LITE
        }
#endif  // !CAPNP_LITE
      }
    }

    KJ_UNREACHABLE;
  }

  static void adopt(SegmentBuilder* segment, WirePointer* ref, OrphanBuilder&& value) {
    KJ_REQUIRE(value.segment == nullptr || value.segment->getArena() == segment->getArena(),
               "Adopted object must live in the same message.");

    if (!ref->isNull()) {
      zeroObject(segment, ref);
    }

    if (value == nullptr) {
      // Set null.
      memset(ref, 0, sizeof(*ref));
    } else if (value.tagAsPtr()->isPositional()) {
      WireHelpers::transferPointer(segment, ref, value.segment, value.tagAsPtr(), value.location);
    } else {
      // FAR and OTHER pointers are position-independent, so we can just copy.
      memcpy(ref, value.tagAsPtr(), sizeof(WirePointer));
    }

    // Take ownership away from the OrphanBuilder.
    memset(value.tagAsPtr(), 0, sizeof(WirePointer));
    value.location = nullptr;
    value.segment = nullptr;
  }

  static OrphanBuilder disown(SegmentBuilder* segment, WirePointer* ref) {
    word* location;

    if (ref->isNull()) {
      location = nullptr;
    } else if (ref->kind() == WirePointer::OTHER) {
      KJ_REQUIRE(ref->isCapability(), "Unknown pointer type.") { break; }
      location = reinterpret_cast<word*>(ref);  // dummy so that it is non-null
    } else {
      WirePointer* refCopy = ref;
      location = followFarsNoWritableCheck(refCopy, ref->target(), segment);
    }

    OrphanBuilder result(ref, segment, location);

    if (!ref->isNull() && ref->isPositional()) {
      result.tagAsPtr()->setKindForOrphan(ref->kind());
    }

    // Zero out the pointer that was disowned.
    memset(ref, 0, sizeof(*ref));

    return result;
  }

  // -----------------------------------------------------------------

  static KJ_ALWAYS_INLINE(StructReader readStructPointer(
      SegmentReader* segment, const WirePointer* ref, const word* defaultValue,
      int nestingLimit)) {
    return readStructPointer(segment, ref, ref->target(), defaultValue, nestingLimit);
  }

  static KJ_ALWAYS_INLINE(StructReader readStructPointer(
      SegmentReader* segment, const WirePointer* ref, const word* refTarget,
      const word* defaultValue, int nestingLimit)) {
    if (ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return StructReader();
      }
      segment = nullptr;
      ref = reinterpret_cast<const WirePointer*>(defaultValue);
      refTarget = ref->target();
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    KJ_REQUIRE(nestingLimit > 0,
               "Message is too deeply-nested or contains cycles.  See capnp::ReaderOptions.") {
      goto useDefault;
    }

    const word* ptr = followFars(ref, refTarget, segment);
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
        nestingLimit - 1);
  }

#if !CAPNP_LITE
  static KJ_ALWAYS_INLINE(kj::Own<ClientHook> readCapabilityPointer(
      SegmentReader* segment, const WirePointer* ref, int nestingLimit)) {
    kj::Maybe<kj::Own<ClientHook>> maybeCap;

    KJ_REQUIRE(brokenCapFactory != nullptr,
               "Trying to read capabilities without ever having created a capability context.  "
               "To read capabilities from a message, you must imbue it with CapReaderContext, or "
               "use the Cap'n Proto RPC system.");

    if (ref->isNull()) {
      return brokenCapFactory->newBrokenCap("Calling null capability pointer.");
    } else if (!ref->isCapability()) {
      KJ_FAIL_REQUIRE(
          "Message contains non-capability pointer where capability pointer was expected.") {
        break;
      }
      return brokenCapFactory->newBrokenCap(
          "Calling capability extracted from a non-capability pointer.");
    } else KJ_IF_MAYBE(cap, segment->getArena()->extractCap(ref->capRef.index.get())) {
      return kj::mv(*cap);
    } else {
      KJ_FAIL_REQUIRE("Message contains invalid capability pointer.") {
        break;
      }
      return brokenCapFactory->newBrokenCap("Calling invalid capability pointer.");
    }
  }
#endif  // !CAPNP_LITE

  static KJ_ALWAYS_INLINE(ListReader readListPointer(
      SegmentReader* segment, const WirePointer* ref, const word* defaultValue,
      ElementSize expectedElementSize, int nestingLimit, bool checkElementSize = true)) {
    return readListPointer(segment, ref, ref->target(), defaultValue,
                           expectedElementSize, nestingLimit, checkElementSize);
  }

  static KJ_ALWAYS_INLINE(ListReader readListPointer(
      SegmentReader* segment, const WirePointer* ref, const word* refTarget,
      const word* defaultValue, ElementSize expectedElementSize, int nestingLimit,
      bool checkElementSize = true)) {
    if (ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr ||
          reinterpret_cast<const WirePointer*>(defaultValue)->isNull()) {
        return ListReader();
      }
      segment = nullptr;
      ref = reinterpret_cast<const WirePointer*>(defaultValue);
      refTarget = ref->target();
      defaultValue = nullptr;  // If the default value is itself invalid, don't use it again.
    }

    KJ_REQUIRE(nestingLimit > 0,
               "Message is too deeply-nested or contains cycles.  See capnp::ReaderOptions.") {
      goto useDefault;
    }

    const word* ptr = followFars(ref, refTarget, segment);
    if (KJ_UNLIKELY(ptr == nullptr)) {
      // Already reported error.
      goto useDefault;
    }

    KJ_REQUIRE(ref->kind() == WirePointer::LIST,
               "Message contains non-list pointer where list pointer was expected.") {
      goto useDefault;
    }

    ElementSize elementSize = ref->listRef.elementSize();
    if (elementSize == ElementSize::INLINE_COMPOSITE) {
      auto wordCount = ref->listRef.inlineCompositeWordCount();

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

      auto size = tag->inlineCompositeListElementCount();
      auto wordsPerElement = tag->structRef.wordSize() / ELEMENTS;

      KJ_REQUIRE(upgradeGuard<uint64_t>(size) * wordsPerElement <= wordCount,
                 "INLINE_COMPOSITE list's elements overrun its word count.") {
        goto useDefault;
      }

      if (wordsPerElement * (ONE * ELEMENTS) == ZERO * WORDS) {
        // Watch out for lists of zero-sized structs, which can claim to be arbitrarily large
        // without having sent actual data.
        KJ_REQUIRE(amplifiedRead(segment, size * (ONE * WORDS / ELEMENTS)),
                   "Message contains amplified list pointer.") {
          goto useDefault;
        }
      }

      if (checkElementSize) {
        // If a struct list was not expected, then presumably a non-struct list was upgraded to a
        // struct list.  We need to manipulate the pointer to point at the first field of the
        // struct.  Together with the "stepBits", this will allow the struct list to be accessed as
        // if it were a primitive list without branching.

        // Check whether the size is compatible.
        switch (expectedElementSize) {
          case ElementSize::VOID:
            break;

          case ElementSize::BIT:
            KJ_FAIL_REQUIRE(
                "Found struct list where bit list was expected; upgrading boolean lists to structs "
                "is no longer supported.") {
              goto useDefault;
            }
            break;

          case ElementSize::BYTE:
          case ElementSize::TWO_BYTES:
          case ElementSize::FOUR_BYTES:
          case ElementSize::EIGHT_BYTES:
            KJ_REQUIRE(tag->structRef.dataSize.get() > ZERO * WORDS,
                       "Expected a primitive list, but got a list of pointer-only structs.") {
              goto useDefault;
            }
            break;

          case ElementSize::POINTER:
            // We expected a list of pointers but got a list of structs.  Assuming the first field
            // in the struct is the pointer we were looking for, we want to munge the pointer to
            // point at the first element's pointer section.
            ptr += tag->structRef.dataSize.get();
            KJ_REQUIRE(tag->structRef.ptrCount.get() > ZERO * POINTERS,
                       "Expected a pointer list, but got a list of data-only structs.") {
              goto useDefault;
            }
            break;

          case ElementSize::INLINE_COMPOSITE:
            break;
        }
      }

      return ListReader(
          segment, ptr, size, wordsPerElement * BITS_PER_WORD,
          tag->structRef.dataSize.get() * BITS_PER_WORD,
          tag->structRef.ptrCount.get(), ElementSize::INLINE_COMPOSITE,
          nestingLimit - 1);

    } else {
      // This is a primitive or pointer list, but all such lists can also be interpreted as struct
      // lists.  We need to compute the data size and pointer count for such structs.
      auto dataSize = dataBitsPerElement(ref->listRef.elementSize()) * ELEMENTS;
      auto pointerCount = pointersPerElement(ref->listRef.elementSize()) * ELEMENTS;
      auto elementCount = ref->listRef.elementCount();
      auto step = (dataSize + pointerCount * BITS_PER_POINTER) / ELEMENTS;

      auto wordCount = roundBitsUpToWords(upgradeGuard<uint64_t>(elementCount) * step);
      KJ_REQUIRE(boundsCheck(segment, ptr, ptr + wordCount),
            "Message contains out-of-bounds list pointer.") {
        goto useDefault;
      }

      if (elementSize == ElementSize::VOID) {
        // Watch out for lists of void, which can claim to be arbitrarily large without having sent
        // actual data.
        KJ_REQUIRE(amplifiedRead(segment, elementCount * (ONE * WORDS / ELEMENTS)),
                   "Message contains amplified list pointer.") {
          goto useDefault;
        }
      }

      if (checkElementSize) {
        if (elementSize == ElementSize::BIT && expectedElementSize != ElementSize::BIT) {
          KJ_FAIL_REQUIRE(
              "Found bit list where struct list was expected; upgrading boolean lists to structs "
              "is no longer supported.") {
            goto useDefault;
          }
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
      }

      return ListReader(segment, ptr, elementCount, step,
                        dataSize, pointerCount, elementSize, nestingLimit - 1);
    }
  }

  static KJ_ALWAYS_INLINE(Text::Reader readTextPointer(
      SegmentReader* segment, const WirePointer* ref,
      const void* defaultValue, ByteCount defaultSize)) {
    return readTextPointer(segment, ref, ref->target(), defaultValue, defaultSize);
  }

  static KJ_ALWAYS_INLINE(Text::Reader readTextPointer(
      SegmentReader* segment, const WirePointer* ref, const word* refTarget,
      const void* defaultValue, ByteCount defaultSize)) {
    if (ref->isNull()) {
    useDefault:
      if (defaultValue == nullptr) defaultValue = "";
      return Text::Reader(reinterpret_cast<const char*>(defaultValue),
          unguard(defaultSize / BYTES));
    } else {
      const word* ptr = followFars(ref, refTarget, segment);

      if (KJ_UNLIKELY(ptr == nullptr)) {
        // Already reported error.
        goto useDefault;
      }

      auto size = ref->listRef.elementCount() * (ONE * BYTES / ELEMENTS);

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
                 "Message contains non-list pointer where text was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(ref->listRef.elementSize() == ElementSize::BYTE,
                 "Message contains list pointer of non-bytes where text was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(boundsCheck(segment, ptr, ptr + roundBytesUpToWords(size)),
                 "Message contained out-of-bounds text pointer.") {
        goto useDefault;
      }

      KJ_REQUIRE(size > ZERO * BYTES, "Message contains text that is not NUL-terminated.") {
        goto useDefault;
      }

      const char* cptr = reinterpret_cast<const char*>(ptr);
      uint unguardedSize = unguard(size / BYTES) - 1;

      KJ_REQUIRE(cptr[unguardedSize] == '\0', "Message contains text that is not NUL-terminated.") {
        goto useDefault;
      }

      return Text::Reader(cptr, unguardedSize);
    }
  }

  static KJ_ALWAYS_INLINE(Data::Reader readDataPointer(
      SegmentReader* segment, const WirePointer* ref,
      const void* defaultValue, BlobSize defaultSize)) {
    return readDataPointer(segment, ref, ref->target(), defaultValue, defaultSize);
  }

  static KJ_ALWAYS_INLINE(Data::Reader readDataPointer(
      SegmentReader* segment, const WirePointer* ref, const word* refTarget,
      const void* defaultValue, BlobSize defaultSize)) {
    if (ref->isNull()) {
    useDefault:
      return Data::Reader(reinterpret_cast<const byte*>(defaultValue),
          unguard(defaultSize / BYTES));
    } else {
      const word* ptr = followFars(ref, refTarget, segment);

      if (KJ_UNLIKELY(ptr == nullptr)) {
        // Already reported error.
        goto useDefault;
      }

      auto size = ref->listRef.elementCount() * (ONE * BYTES / ELEMENTS);

      KJ_REQUIRE(ref->kind() == WirePointer::LIST,
                 "Message contains non-list pointer where data was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(ref->listRef.elementSize() == ElementSize::BYTE,
                 "Message contains list pointer of non-bytes where data was expected.") {
        goto useDefault;
      }

      KJ_REQUIRE(boundsCheck(segment, ptr, ptr + roundBytesUpToWords(size)),
                 "Message contained out-of-bounds data pointer.") {
        goto useDefault;
      }

      return Data::Reader(reinterpret_cast<const byte*>(ptr), unguard(size / BYTES));
    }
  }
};

// =======================================================================================
// PointerBuilder

StructBuilder PointerBuilder::initStruct(StructSize size) {
  return WireHelpers::initStructPointer(pointer, segment, size);
}

StructBuilder PointerBuilder::getStruct(StructSize size, const word* defaultValue) {
  return WireHelpers::getWritableStructPointer(pointer, segment, size, defaultValue);
}

ListBuilder PointerBuilder::initList(ElementSize elementSize, ElementCount elementCount) {
  return WireHelpers::initListPointer(pointer, segment, elementCount, elementSize);
}

ListBuilder PointerBuilder::initStructList(ElementCount elementCount, StructSize elementSize) {
  return WireHelpers::initStructListPointer(pointer, segment, elementCount, elementSize);
}

ListBuilder PointerBuilder::getList(ElementSize elementSize, const word* defaultValue) {
  return WireHelpers::getWritableListPointer(pointer, segment, elementSize, defaultValue);
}

ListBuilder PointerBuilder::getStructList(StructSize elementSize, const word* defaultValue) {
  return WireHelpers::getWritableStructListPointer(pointer, segment, elementSize, defaultValue);
}

ListBuilder PointerBuilder::getListAnySize(const word* defaultValue) {
  return WireHelpers::getWritableListPointerAnySize(pointer, segment, defaultValue);
}

template <>
Text::Builder PointerBuilder::initBlob<Text>(ByteCount size) {
  return WireHelpers::initTextPointer(pointer, segment,
      assertMax<MAX_TEXT_SIZE>(size, ThrowOverflow())).value;
}
template <>
void PointerBuilder::setBlob<Text>(Text::Reader value) {
  WireHelpers::setTextPointer(pointer, segment, value);
}
template <>
Text::Builder PointerBuilder::getBlob<Text>(const void* defaultValue, ByteCount defaultSize) {
  return WireHelpers::getWritableTextPointer(pointer, segment, defaultValue,
      assertMax<MAX_TEXT_SIZE>(defaultSize, ThrowOverflow()));
}

template <>
Data::Builder PointerBuilder::initBlob<Data>(ByteCount size) {
  return WireHelpers::initDataPointer(pointer, segment,
      assertMaxBits<BLOB_SIZE_BITS>(size, ThrowOverflow())).value;
}
template <>
void PointerBuilder::setBlob<Data>(Data::Reader value) {
  WireHelpers::setDataPointer(pointer, segment, value);
}
template <>
Data::Builder PointerBuilder::getBlob<Data>(const void* defaultValue, ByteCount defaultSize) {
  return WireHelpers::getWritableDataPointer(pointer, segment, defaultValue,
      assertMaxBits<BLOB_SIZE_BITS>(defaultSize, ThrowOverflow()));
}

void PointerBuilder::setStruct(const StructReader& value) {
  WireHelpers::setStructPointer(segment, pointer, value);
}

void PointerBuilder::setList(const ListReader& value) {
  WireHelpers::setListPointer(segment, pointer, value);
}

#if !CAPNP_LITE
kj::Own<ClientHook> PointerBuilder::getCapability() {
  return WireHelpers::readCapabilityPointer(
      segment, pointer, kj::maxValue);
}

void PointerBuilder::setCapability(kj::Own<ClientHook>&& cap) {
  WireHelpers::setCapabilityPointer(segment, pointer, kj::mv(cap));
}
#endif  // !CAPNP_LITE

void PointerBuilder::adopt(OrphanBuilder&& value) {
  WireHelpers::adopt(segment, pointer, kj::mv(value));
}

OrphanBuilder PointerBuilder::disown() {
  return WireHelpers::disown(segment, pointer);
}

void PointerBuilder::clear() {
  WireHelpers::zeroObject(segment, pointer);
  memset(pointer, 0, sizeof(WirePointer));
}

bool PointerBuilder::isNull() {
  return pointer->isNull();
}

bool PointerBuilder::isStruct() {
  WirePointer* ptr = pointer;
  WireHelpers::followFars(ptr, ptr->target(), segment);
  return ptr->kind() == WirePointer::Kind::STRUCT;
}

bool PointerBuilder::isList() {
  WirePointer* ptr = pointer;
  WireHelpers::followFars(ptr, ptr->target(), segment);
  return ptr->kind() == WirePointer::Kind::LIST;
}

void PointerBuilder::transferFrom(PointerBuilder other) {
  if (!pointer->isNull()) {
    WireHelpers::zeroObject(segment, pointer);
    memset(pointer, 0, sizeof(*pointer));
  }
  WireHelpers::transferPointer(segment, pointer, other.segment, other.pointer);
}

void PointerBuilder::copyFrom(PointerReader other) {
  if (!pointer->isNull()) {
    WireHelpers::zeroObject(segment, pointer);
    memset(pointer, 0, sizeof(*pointer));
  }
  if (other.pointer != nullptr) {
    WireHelpers::copyPointer(segment, pointer, other.segment, other.pointer, other.nestingLimit);
  }
}

PointerReader PointerBuilder::asReader() const {
  return PointerReader(segment, pointer, kj::maxValue);
}

BuilderArena* PointerBuilder::getArena() const {
  return segment->getArena();
}

// =======================================================================================
// PointerReader

PointerReader PointerReader::getRoot(SegmentReader* segment, const word* location,
                                     int nestingLimit) {
  KJ_REQUIRE(WireHelpers::boundsCheck(segment, location, location + POINTER_SIZE_IN_WORDS),
             "Root location out-of-bounds.") {
    location = nullptr;
  }

  return PointerReader(segment, reinterpret_cast<const WirePointer*>(location), nestingLimit);
}

StructReader PointerReader::getStruct(const word* defaultValue) const {
  const WirePointer* ref = pointer == nullptr ? &zero.pointer : pointer;
  return WireHelpers::readStructPointer(segment, ref, defaultValue, nestingLimit);
}

ListReader PointerReader::getList(ElementSize expectedElementSize, const word* defaultValue) const {
  const WirePointer* ref = pointer == nullptr ? &zero.pointer : pointer;
  return WireHelpers::readListPointer(
      segment, ref, defaultValue, expectedElementSize, nestingLimit);
}

ListReader PointerReader::getListAnySize(const word* defaultValue) const {
  const WirePointer* ref = pointer == nullptr ? &zero.pointer : pointer;
  return WireHelpers::readListPointer(
      segment, ref, defaultValue, ElementSize::VOID /* dummy */, nestingLimit, false);
}

template <>
Text::Reader PointerReader::getBlob<Text>(const void* defaultValue, ByteCount defaultSize) const {
  const WirePointer* ref = pointer == nullptr ? &zero.pointer : pointer;
  return WireHelpers::readTextPointer(segment, ref, defaultValue, defaultSize);
}

template <>
Data::Reader PointerReader::getBlob<Data>(const void* defaultValue, ByteCount defaultSize) const {
  const WirePointer* ref = pointer == nullptr ? &zero.pointer : pointer;
  return WireHelpers::readDataPointer(segment, ref, defaultValue,
      assertMaxBits<BLOB_SIZE_BITS>(defaultSize, ThrowOverflow()));
}

#if !CAPNP_LITE
kj::Own<ClientHook> PointerReader::getCapability() const {
  const WirePointer* ref = pointer == nullptr ? &zero.pointer : pointer;
  return WireHelpers::readCapabilityPointer(segment, ref, nestingLimit);
}
#endif  // !CAPNP_LITE

const word* PointerReader::getUnchecked() const {
  KJ_REQUIRE(segment == nullptr, "getUncheckedPointer() only allowed on unchecked messages.");
  return reinterpret_cast<const word*>(pointer);
}

MessageSizeCounts PointerReader::targetSize() const {
  return pointer == nullptr ? MessageSizeCounts { ZERO * WORDS, 0 }
                            : WireHelpers::totalSize(segment, pointer, nestingLimit);
}

bool PointerReader::isNull() const {
  return pointer == nullptr || pointer->isNull();
}

bool PointerReader::isStruct() const {
  word* refTarget = nullptr;
  const WirePointer* ptr = pointer;
  SegmentReader* sgmt = segment;
  WireHelpers::followFars(ptr, refTarget, sgmt);
  return ptr->kind() == WirePointer::Kind::STRUCT;
}

bool PointerReader::isList() const {
  word* refTarget = nullptr;
  const WirePointer* ptr = pointer;
  SegmentReader* sgmt = segment;
  WireHelpers::followFars(ptr, refTarget, sgmt);
  return ptr->kind() == WirePointer::Kind::LIST;
}

kj::Maybe<Arena&> PointerReader::getArena() const {
  return segment == nullptr ? nullptr : segment->getArena();
}

// =======================================================================================
// StructBuilder

void StructBuilder::clearAll() {
  if (dataSize == ONE * BITS) {
    setDataField<bool>(ONE * ELEMENTS, false);
  } else {
    WireHelpers::zeroMemory(reinterpret_cast<byte*>(data), dataSize / BITS_PER_BYTE);
  }

  for (auto i: kj::zeroTo(pointerCount)) {
    WireHelpers::zeroObject(segment, pointers + i);
  }
  WireHelpers::zeroMemory(pointers, pointerCount);
}

void StructBuilder::transferContentFrom(StructBuilder other) {
  // Determine the amount of data the builders have in common.
  auto sharedDataSize = kj::min(dataSize, other.dataSize);

  if (dataSize > sharedDataSize) {
    // Since the target is larger than the source, make sure to zero out the extra bits that the
    // source doesn't have.
    if (dataSize == ONE * BITS) {
      setDataField<bool>(ZERO * ELEMENTS, false);
    } else {
      byte* unshared = reinterpret_cast<byte*>(data) + sharedDataSize / BITS_PER_BYTE;
      // Note: this subtraction can't fail due to the if() above
      WireHelpers::zeroMemory(unshared,
          subtractChecked(dataSize, sharedDataSize, []() {}) / BITS_PER_BYTE);
    }
  }

  // Copy over the shared part.
  if (sharedDataSize == ONE * BITS) {
    setDataField<bool>(ZERO * ELEMENTS, other.getDataField<bool>(ZERO * ELEMENTS));
  } else {
    WireHelpers::copyMemory(reinterpret_cast<byte*>(data),
                            reinterpret_cast<byte*>(other.data),
                            sharedDataSize / BITS_PER_BYTE);
  }

  // Zero out all pointers in the target.
  for (auto i: kj::zeroTo(pointerCount)) {
    WireHelpers::zeroObject(segment, pointers + i);
  }
  WireHelpers::zeroMemory(pointers, pointerCount);

  // Transfer the pointers.
  auto sharedPointerCount = kj::min(pointerCount, other.pointerCount);
  for (auto i: kj::zeroTo(sharedPointerCount)) {
    WireHelpers::transferPointer(segment, pointers + i, other.segment, other.pointers + i);
  }

  // Zero out the pointers that were transferred in the source because it no longer has ownership.
  // If the source had any extra pointers that the destination didn't have space for, we
  // intentionally leave them be, so that they'll be cleaned up later.
  WireHelpers::zeroMemory(other.pointers, sharedPointerCount);
}

void StructBuilder::copyContentFrom(StructReader other) {
  // Determine the amount of data the builders have in common.
  auto sharedDataSize = kj::min(dataSize, other.dataSize);

  if (dataSize > sharedDataSize) {
    // Since the target is larger than the source, make sure to zero out the extra bits that the
    // source doesn't have.
    if (dataSize == ONE * BITS) {
      setDataField<bool>(ZERO * ELEMENTS, false);
    } else {
      byte* unshared = reinterpret_cast<byte*>(data) + sharedDataSize / BITS_PER_BYTE;
      WireHelpers::zeroMemory(unshared,
          subtractChecked(dataSize, sharedDataSize, []() {}) / BITS_PER_BYTE);
    }
  }

  // Copy over the shared part.
  if (sharedDataSize == ONE * BITS) {
    setDataField<bool>(ZERO * ELEMENTS, other.getDataField<bool>(ZERO * ELEMENTS));
  } else {
    WireHelpers::copyMemory(reinterpret_cast<byte*>(data),
                            reinterpret_cast<const byte*>(other.data),
                            sharedDataSize / BITS_PER_BYTE);
  }

  // Zero out all pointers in the target.
  for (auto i: kj::zeroTo(pointerCount)) {
    WireHelpers::zeroObject(segment, pointers + i);
  }
  WireHelpers::zeroMemory(pointers, pointerCount);

  // Copy the pointers.
  auto sharedPointerCount = kj::min(pointerCount, other.pointerCount);
  for (auto i: kj::zeroTo(sharedPointerCount)) {
    WireHelpers::copyPointer(segment, pointers + i,
        other.segment, other.pointers + i, other.nestingLimit);
  }
}

StructReader StructBuilder::asReader() const {
  return StructReader(segment, data, pointers,
      dataSize, pointerCount, kj::maxValue);
}

BuilderArena* StructBuilder::getArena() {
  return segment->getArena();
}

// =======================================================================================
// StructReader

MessageSizeCounts StructReader::totalSize() const {
  MessageSizeCounts result = {
    WireHelpers::roundBitsUpToWords(dataSize) + pointerCount * WORDS_PER_POINTER, 0 };

  for (auto i: kj::zeroTo(pointerCount)) {
    result += WireHelpers::totalSize(segment, pointers + i, nestingLimit);
  }

  if (segment != nullptr) {
    // This traversal should not count against the read limit, because it's highly likely that
    // the caller is going to traverse the object again, e.g. to copy it.
    segment->unread(result.wordCount);
  }

  return result;
}

// =======================================================================================
// ListBuilder

Text::Builder ListBuilder::asText() {
  KJ_REQUIRE(structDataSize == G(8) * BITS && structPointerCount == ZERO * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Text::Builder();
  }

  size_t size = unguard(elementCount / ELEMENTS);

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
  KJ_REQUIRE(structDataSize == G(8) * BITS && structPointerCount == ZERO * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Data::Builder();
  }

  return Data::Builder(reinterpret_cast<byte*>(ptr), unguard(elementCount / ELEMENTS));
}

StructBuilder ListBuilder::getStructElement(ElementCount index) {
  auto indexBit = upgradeGuard<uint64_t>(index) * step;
  byte* structData = ptr + indexBit / BITS_PER_BYTE;
  KJ_DASSERT(indexBit % BITS_PER_BYTE == ZERO * BITS);
  return StructBuilder(segment, structData,
      reinterpret_cast<WirePointer*>(structData + structDataSize / BITS_PER_BYTE),
      structDataSize, structPointerCount);
}

ListReader ListBuilder::asReader() const {
  return ListReader(segment, ptr, elementCount, step, structDataSize, structPointerCount,
                    elementSize, kj::maxValue);
}

BuilderArena* ListBuilder::getArena() {
  return segment->getArena();
}

// =======================================================================================
// ListReader

Text::Reader ListReader::asText() {
  KJ_REQUIRE(structDataSize == G(8) * BITS && structPointerCount == ZERO * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Text::Reader();
  }

  size_t size = unguard(elementCount / ELEMENTS);

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
  KJ_REQUIRE(structDataSize == G(8) * BITS && structPointerCount == ZERO * POINTERS,
             "Expected Text, got list of non-bytes.") {
    return Data::Reader();
  }

  return Data::Reader(reinterpret_cast<const byte*>(ptr), unguard(elementCount / ELEMENTS));
}

StructReader ListReader::getStructElement(ElementCount index) const {
  KJ_REQUIRE(nestingLimit > 0,
             "Message is too deeply-nested or contains cycles.  See capnp::ReaderOptions.") {
    return StructReader();
  }

  auto indexBit = upgradeGuard<uint64_t>(index) * step;
  const byte* structData = ptr + indexBit / BITS_PER_BYTE;
  const WirePointer* structPointers =
      reinterpret_cast<const WirePointer*>(structData + structDataSize / BITS_PER_BYTE);

  // This check should pass if there are no bugs in the list pointer validation code.
  KJ_DASSERT(structPointerCount == ZERO * POINTERS ||
         (uintptr_t)structPointers % sizeof(void*) == 0,
         "Pointer section of struct list element not aligned.");

  KJ_DASSERT(indexBit % BITS_PER_BYTE == ZERO * BITS);
  return StructReader(
      segment, structData, structPointers,
      structDataSize, structPointerCount,
      nestingLimit - 1);
}

// =======================================================================================
// OrphanBuilder

OrphanBuilder OrphanBuilder::initStruct(BuilderArena* arena, StructSize size) {
  OrphanBuilder result;
  StructBuilder builder = WireHelpers::initStructPointer(result.tagAsPtr(), nullptr, size, arena);
  result.segment = builder.segment;
  result.location = builder.getLocation();
  return result;
}

OrphanBuilder OrphanBuilder::initList(
    BuilderArena* arena, ElementCount elementCount, ElementSize elementSize) {
  OrphanBuilder result;
  ListBuilder builder = WireHelpers::initListPointer(
      result.tagAsPtr(), nullptr, elementCount, elementSize, arena);
  result.segment = builder.segment;
  result.location = builder.getLocation();
  return result;
}

OrphanBuilder OrphanBuilder::initStructList(
    BuilderArena* arena, ElementCount elementCount, StructSize elementSize) {
  OrphanBuilder result;
  ListBuilder builder = WireHelpers::initStructListPointer(
      result.tagAsPtr(), nullptr, elementCount, elementSize, arena);
  result.segment = builder.segment;
  result.location = builder.getLocation();
  return result;
}

OrphanBuilder OrphanBuilder::initText(BuilderArena* arena, ByteCount size) {
  OrphanBuilder result;
  auto allocation = WireHelpers::initTextPointer(result.tagAsPtr(), nullptr,
      assertMax<MAX_TEXT_SIZE>(size, ThrowOverflow()), arena);
  result.segment = allocation.segment;
  result.location = reinterpret_cast<word*>(allocation.value.begin());
  return result;
}

OrphanBuilder OrphanBuilder::initData(BuilderArena* arena, ByteCount size) {
  OrphanBuilder result;
  auto allocation = WireHelpers::initDataPointer(result.tagAsPtr(), nullptr,
      assertMaxBits<BLOB_SIZE_BITS>(size), arena);
  result.segment = allocation.segment;
  result.location = reinterpret_cast<word*>(allocation.value.begin());
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, StructReader copyFrom) {
  OrphanBuilder result;
  auto allocation = WireHelpers::setStructPointer(nullptr, result.tagAsPtr(), copyFrom, arena);
  result.segment = allocation.segment;
  result.location = reinterpret_cast<word*>(allocation.value);
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, ListReader copyFrom) {
  OrphanBuilder result;
  auto allocation = WireHelpers::setListPointer(nullptr, result.tagAsPtr(), copyFrom, arena);
  result.segment = allocation.segment;
  result.location = reinterpret_cast<word*>(allocation.value);
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, PointerReader copyFrom) {
  OrphanBuilder result;
  auto allocation = WireHelpers::copyPointer(
      nullptr, result.tagAsPtr(), copyFrom.segment, copyFrom.pointer, copyFrom.nestingLimit, arena);
  result.segment = allocation.segment;
  result.location = reinterpret_cast<word*>(allocation.value);
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, Text::Reader copyFrom) {
  OrphanBuilder result;
  auto allocation = WireHelpers::setTextPointer(
      result.tagAsPtr(), nullptr, copyFrom, arena);
  result.segment = allocation.segment;
  result.location = reinterpret_cast<word*>(allocation.value.begin());
  return result;
}

OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, Data::Reader copyFrom) {
  OrphanBuilder result;
  auto allocation = WireHelpers::setDataPointer(
      result.tagAsPtr(), nullptr, copyFrom, arena);
  result.segment = allocation.segment;
  result.location = reinterpret_cast<word*>(allocation.value.begin());
  return result;
}

#if !CAPNP_LITE
OrphanBuilder OrphanBuilder::copy(BuilderArena* arena, kj::Own<ClientHook> copyFrom) {
  OrphanBuilder result;
  WireHelpers::setCapabilityPointer(nullptr, result.tagAsPtr(), kj::mv(copyFrom), arena);
  result.segment = arena->getSegment(SegmentId(0));
  result.location = &result.tag;  // dummy to make location non-null
  return result;
}
#endif  // !CAPNP_LITE

OrphanBuilder OrphanBuilder::referenceExternalData(BuilderArena* arena, Data::Reader data) {
  KJ_REQUIRE(reinterpret_cast<uintptr_t>(data.begin()) % sizeof(void*) == 0,
             "Cannot referenceExternalData() that is not aligned.");

  auto checkedSize = assertMaxBits<BLOB_SIZE_BITS>(guarded(data.size()));
  auto wordCount = WireHelpers::roundBytesUpToWords(checkedSize * BYTES);
  kj::ArrayPtr<const word> words(reinterpret_cast<const word*>(data.begin()),
                                 unguard(wordCount / WORDS));

  OrphanBuilder result;
  result.tagAsPtr()->setKindForOrphan(WirePointer::LIST);
  result.tagAsPtr()->listRef.set(ElementSize::BYTE, checkedSize * ELEMENTS);
  result.segment = arena->addExternalSegment(words);

  // const_cast OK here because we will check whether the segment is writable when we try to get
  // a builder.
  result.location = const_cast<word*>(words.begin());

  return result;
}

StructBuilder OrphanBuilder::asStruct(StructSize size) {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));

  StructBuilder result = WireHelpers::getWritableStructPointer(
      tagAsPtr(), location, segment, size, nullptr, segment->getArena());

  // Watch out, the pointer could have been updated if the object had to be relocated.
  location = reinterpret_cast<word*>(result.data);

  return result;
}

ListBuilder OrphanBuilder::asList(ElementSize elementSize) {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));

  ListBuilder result = WireHelpers::getWritableListPointer(
      tagAsPtr(), location, segment, elementSize, nullptr, segment->getArena());

  // Watch out, the pointer could have been updated if the object had to be relocated.
  // (Actually, currently this is not true for primitive lists, but let's not turn into a bug if
  // it changes!)
  location = result.getLocation();

  return result;
}

ListBuilder OrphanBuilder::asStructList(StructSize elementSize) {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));

  ListBuilder result = WireHelpers::getWritableStructListPointer(
      tagAsPtr(), location, segment, elementSize, nullptr, segment->getArena());

  // Watch out, the pointer could have been updated if the object had to be relocated.
  location = result.getLocation();

  return result;
}

Text::Builder OrphanBuilder::asText() {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));

  // Never relocates.
  return WireHelpers::getWritableTextPointer(tagAsPtr(), location, segment, nullptr, ZERO * BYTES);
}

Data::Builder OrphanBuilder::asData() {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));

  // Never relocates.
  return WireHelpers::getWritableDataPointer(tagAsPtr(), location, segment, nullptr, ZERO * BYTES);
}

StructReader OrphanBuilder::asStructReader(StructSize size) const {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));
  return WireHelpers::readStructPointer(
      segment, tagAsPtr(), location, nullptr, kj::maxValue);
}

ListReader OrphanBuilder::asListReader(ElementSize elementSize) const {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));
  return WireHelpers::readListPointer(
      segment, tagAsPtr(), location, nullptr, elementSize, kj::maxValue);
}

#if !CAPNP_LITE
kj::Own<ClientHook> OrphanBuilder::asCapability() const {
  return WireHelpers::readCapabilityPointer(segment, tagAsPtr(), kj::maxValue);
}
#endif  // !CAPNP_LITE

Text::Reader OrphanBuilder::asTextReader() const {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));
  return WireHelpers::readTextPointer(segment, tagAsPtr(), location, nullptr, ZERO * BYTES);
}

Data::Reader OrphanBuilder::asDataReader() const {
  KJ_DASSERT(tagAsPtr()->isNull() == (location == nullptr));
  return WireHelpers::readDataPointer(segment, tagAsPtr(), location, nullptr, ZERO * BYTES);
}

void OrphanBuilder::truncate(ElementCount size, bool isText) {
  ElementCountN<LIST_ELEMENT_COUNT_BITS> checkedSize;

  if (isText) {
    checkedSize = assertMax<MAX_TEXT_SIZE>(size, ThrowOverflow()) + ONE * ELEMENTS;
  } else {
    checkedSize = assertMaxBits<BLOB_SIZE_BITS>(size, ThrowOverflow());
  }

  WirePointer* ref = tagAsPtr();
  SegmentBuilder* segment = this->segment;

  word* target = WireHelpers::followFars(ref, location, segment);

  KJ_REQUIRE(ref->kind() == WirePointer::LIST, "Can't truncate non-list.") {
    return;
  }

  // TODO(someday): Implement truncation of all sizes.
  KJ_ASSERT(ref->listRef.elementSize() == ElementSize::BYTE,
            "Not implemented: truncate non-blob.");

  auto oldSize = ref->listRef.elementCount();
  KJ_REQUIRE(checkedSize <= oldSize, "Truncate size must be smaller than existing size.") {
    return;
  }

  ref->listRef.set(ref->listRef.elementSize(), checkedSize);

  byte* begin = reinterpret_cast<byte*>(target);
  byte* truncPoint = begin + checkedSize * (ONE * BYTES / ELEMENTS);
  byte* end = begin + oldSize * (ONE * BYTES / ELEMENTS);
  memset(truncPoint - isText, 0, end - truncPoint + isText);

  word* truncWord = target + WireHelpers::roundBytesUpToWords(
      checkedSize * (ONE * BYTES / ELEMENTS));
  word* endWord = target + WireHelpers::roundBytesUpToWords(oldSize * (ONE * BYTES / ELEMENTS));

  segment->tryTruncate(endWord, truncWord);
}

void OrphanBuilder::euthanize() {
  // Carefully catch any exceptions and rethrow them as recoverable exceptions since we may be in
  // a destructor.
  auto exception = kj::runCatchingExceptions([&]() {
    if (tagAsPtr()->isPositional()) {
      WireHelpers::zeroObject(segment, tagAsPtr(), location);
    } else {
      WireHelpers::zeroObject(segment, tagAsPtr());
    }

    memset(&tag, 0, sizeof(tag));
    segment = nullptr;
    location = nullptr;
  });

  KJ_IF_MAYBE(e, exception) {
    kj::getExceptionCallback().onRecoverableException(kj::mv(*e));
  }
}

}  // namespace _ (private)
}  // namespace capnp
