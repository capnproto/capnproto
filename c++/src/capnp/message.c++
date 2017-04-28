// Copyright (c) 2013-2016 Sandstorm Development Group, Inc. and contributors
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
#include "message.h"
#include <kj/debug.h>
#include "arena.h"
#include "orphan.h"
#include <stdlib.h>
#include <exception>
#include <string>
#include <vector>
#include <errno.h>

namespace capnp {

namespace {

class DummyCapTableReader: public _::CapTableReader {
public:
#if !CAPNP_LITE
  kj::Maybe<kj::Own<ClientHook>> extractCap(uint index) override {
    return nullptr;
  }
#endif
};
static KJ_CONSTEXPR(const) DummyCapTableReader dummyCapTableReader = DummyCapTableReader();

}  // namespace

MessageReader::MessageReader(ReaderOptions options): options(options), allocatedArena(false) {}
MessageReader::~MessageReader() noexcept(false) {
  if (allocatedArena) {
    arena()->~ReaderArena();
  }
}

bool MessageReader::isCanonical() {
  if (!allocatedArena) {
    static_assert(sizeof(_::ReaderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a ReaderArena.  Please increase it.  This will break "
        "ABI compatibility.");
    new(arena()) _::ReaderArena(this);
    allocatedArena = true;
  }

  _::SegmentReader *segment = arena()->tryGetSegment(_::SegmentId(0));

  if (segment == NULL) {
    // The message has no segments
    return false;
  }

  if (arena()->tryGetSegment(_::SegmentId(1))) {
    // The message has more than one segment
    return false;
  }

  const word* readHead = segment->getStartPtr() + 1;
  bool rootIsCanonical = _::PointerReader::getRoot(segment, nullptr,
                                                   segment->getStartPtr(),
                                                   this->getOptions().nestingLimit)
                                                  .isCanonical(&readHead);
  bool allWordsConsumed = segment->getOffsetTo(readHead) == segment->getSize();
  return rootIsCanonical && allWordsConsumed;
}


AnyPointer::Reader MessageReader::getRootInternal() {
  if (!allocatedArena) {
    static_assert(sizeof(_::ReaderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a ReaderArena.  Please increase it.  This will break "
        "ABI compatibility.");
    new(arena()) _::ReaderArena(this);
    allocatedArena = true;
  }

  _::SegmentReader* segment = arena()->tryGetSegment(_::SegmentId(0));
  KJ_REQUIRE(segment != nullptr &&
             segment->checkObject(segment->getStartPtr(), ONE * WORDS),
             "Message did not contain a root pointer.") {
    return AnyPointer::Reader();
  }

  // const_cast here is safe because dummyCapTableReader has no state.
  return AnyPointer::Reader(_::PointerReader::getRoot(
      segment, const_cast<DummyCapTableReader*>(&dummyCapTableReader),
      segment->getStartPtr(), options.nestingLimit));
}

// -------------------------------------------------------------------

MessageBuilder::MessageBuilder(): allocatedArena(false) {}

MessageBuilder::~MessageBuilder() noexcept(false) {
  if (allocatedArena) {
    kj::dtor(*arena());
  }
}

MessageBuilder::MessageBuilder(kj::ArrayPtr<SegmentInit> segments)
    : allocatedArena(false) {
  kj::ctor(*arena(), this, segments);
  allocatedArena = true;
}

_::SegmentBuilder* MessageBuilder::getRootSegment() {
  if (allocatedArena) {
    return arena()->getSegment(_::SegmentId(0));
  } else {
    static_assert(sizeof(_::BuilderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a BuilderArena.  Please increase it.");
    kj::ctor(*arena(), this);
    allocatedArena = true;

    auto allocation = arena()->allocate(POINTER_SIZE_IN_WORDS);

    KJ_ASSERT(allocation.segment->getSegmentId() == _::SegmentId(0),
        "First allocated word of new arena was not in segment ID 0.");
    KJ_ASSERT(allocation.words == allocation.segment->getPtrUnchecked(ZERO * WORDS),
        "First allocated word of new arena was not the first word in its segment.");
    return allocation.segment;
  }
}

AnyPointer::Builder MessageBuilder::getRootInternal() {
  _::SegmentBuilder* rootSegment = getRootSegment();
  return AnyPointer::Builder(_::PointerBuilder::getRoot(
      rootSegment, arena()->getLocalCapTable(), rootSegment->getPtrUnchecked(ZERO * WORDS)));
}

kj::ArrayPtr<const kj::ArrayPtr<const word>> MessageBuilder::getSegmentsForOutput() {
  if (allocatedArena) {
    return arena()->getSegmentsForOutput();
  } else {
    return nullptr;
  }
}

Orphanage MessageBuilder::getOrphanage() {
  // We must ensure that the arena and root pointer have been allocated before the Orphanage
  // can be used.
  if (!allocatedArena) getRootSegment();

  return Orphanage(arena(), arena()->getLocalCapTable());
}

bool MessageBuilder::isCanonical() {
  _::SegmentReader *segment = getRootSegment();

  if (segment == NULL) {
    // The message has no segments
    return false;
  }

  if (arena()->tryGetSegment(_::SegmentId(1))) {
    // The message has more than one segment
    return false;
  }

  const word* readHead = segment->getStartPtr() + 1;
  return _::PointerReader::getRoot(segment, nullptr, segment->getStartPtr(), kj::maxValue)
                                  .isCanonical(&readHead);
}

// =======================================================================================

SegmentArrayMessageReader::SegmentArrayMessageReader(
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments, ReaderOptions options)
    : MessageReader(options), segments(segments) {}

SegmentArrayMessageReader::~SegmentArrayMessageReader() noexcept(false) {}

kj::ArrayPtr<const word> SegmentArrayMessageReader::getSegment(uint id) {
  if (id < segments.size()) {
    return segments[id];
  } else {
    return nullptr;
  }
}

// -------------------------------------------------------------------

struct MallocMessageBuilder::MoreSegments {
  std::vector<void*> segments;
};

MallocMessageBuilder::MallocMessageBuilder(
    uint firstSegmentWords, AllocationStrategy allocationStrategy)
    : nextSize(firstSegmentWords), allocationStrategy(allocationStrategy),
      ownFirstSegment(true), returnedFirstSegment(false), firstSegment(nullptr) {}

MallocMessageBuilder::MallocMessageBuilder(
    kj::ArrayPtr<word> firstSegment, AllocationStrategy allocationStrategy)
    : nextSize(firstSegment.size()), allocationStrategy(allocationStrategy),
      ownFirstSegment(false), returnedFirstSegment(false), firstSegment(firstSegment.begin()) {
  KJ_REQUIRE(firstSegment.size() > 0, "First segment size must be non-zero.");

  // Checking just the first word should catch most cases of failing to zero the segment.
  KJ_REQUIRE(*reinterpret_cast<uint64_t*>(firstSegment.begin()) == 0,
          "First segment must be zeroed.");
}

MallocMessageBuilder::~MallocMessageBuilder() noexcept(false) {
  if (returnedFirstSegment) {
    if (ownFirstSegment) {
      free(firstSegment);
    } else {
      // Must zero first segment.
      kj::ArrayPtr<const kj::ArrayPtr<const word>> segments = getSegmentsForOutput();
      if (segments.size() > 0) {
        KJ_ASSERT(segments[0].begin() == firstSegment,
            "First segment in getSegmentsForOutput() is not the first segment allocated?");
        memset(firstSegment, 0, segments[0].size() * sizeof(word));
      }
    }

    KJ_IF_MAYBE(s, moreSegments) {
      for (void* ptr: s->get()->segments) {
        free(ptr);
      }
    }
  }
}

kj::ArrayPtr<word> MallocMessageBuilder::allocateSegment(uint minimumSize) {
  KJ_REQUIRE(bounded(minimumSize) * WORDS <= MAX_SEGMENT_WORDS,
      "MallocMessageBuilder asked to allocate segment above maximum serializable size.");
  KJ_ASSERT(bounded(nextSize) * WORDS <= MAX_SEGMENT_WORDS,
      "MallocMessageBuilder nextSize out of bounds.");

  if (!returnedFirstSegment && !ownFirstSegment) {
    kj::ArrayPtr<word> result = kj::arrayPtr(reinterpret_cast<word*>(firstSegment), nextSize);
    if (result.size() >= minimumSize) {
      returnedFirstSegment = true;
      return result;
    }
    // If the provided first segment wasn't big enough, we discard it and proceed to allocate
    // our own.  This never happens in practice since minimumSize is always 1 for the first
    // segment.
    ownFirstSegment = true;
  }

  uint size = kj::max(minimumSize, nextSize);

  void* result = calloc(size, sizeof(word));
  if (result == nullptr) {
    KJ_FAIL_SYSCALL("calloc(size, sizeof(word))", ENOMEM, size);
  }

  if (!returnedFirstSegment) {
    firstSegment = result;
    returnedFirstSegment = true;

    // After the first segment, we want nextSize to equal the total size allocated so far.
    if (allocationStrategy == AllocationStrategy::GROW_HEURISTICALLY) nextSize = size;
  } else {
    MoreSegments* segments;
    KJ_IF_MAYBE(s, moreSegments) {
      segments = *s;
    } else {
      auto newSegments = kj::heap<MoreSegments>();
      segments = newSegments;
      moreSegments = mv(newSegments);
    }
    segments->segments.push_back(result);
    if (allocationStrategy == AllocationStrategy::GROW_HEURISTICALLY) {
      // set nextSize = min(nextSize+size, MAX_SEGMENT_WORDS)
      // while protecting against possible overflow of (nextSize+size)
      nextSize = (size <= unbound(MAX_SEGMENT_WORDS / WORDS) - nextSize)
          ? nextSize + size : unbound(MAX_SEGMENT_WORDS / WORDS);
    }
  }

  return kj::arrayPtr(reinterpret_cast<word*>(result), size);
}

// -------------------------------------------------------------------

FlatMessageBuilder::FlatMessageBuilder(kj::ArrayPtr<word> array): array(array), allocated(false) {}
FlatMessageBuilder::~FlatMessageBuilder() noexcept(false) {}

void FlatMessageBuilder::requireFilled() {
  KJ_REQUIRE(getSegmentsForOutput()[0].end() == array.end(),
          "FlatMessageBuilder's buffer was too large.");
}

kj::ArrayPtr<word> FlatMessageBuilder::allocateSegment(uint minimumSize) {
  KJ_REQUIRE(!allocated, "FlatMessageBuilder's buffer was not large enough.");
  allocated = true;
  return array;
}

}  // namespace capnp
