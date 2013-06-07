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
#include "message.h"
#include <kj/debug.h>
#include "arena.h"
#include <stdlib.h>
#include <exception>
#include <string>
#include <vector>
#include <unistd.h>

namespace capnp {

MessageReader::MessageReader(ReaderOptions options): options(options), allocatedArena(false) {}
MessageReader::~MessageReader() noexcept(false) {
  if (allocatedArena) {
    arena()->~ReaderArena();
  }
}

_::StructReader MessageReader::getRootInternal() {
  if (!allocatedArena) {
    static_assert(sizeof(_::ReaderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a ReaderArena.  Please increase it.  This will break "
        "ABI compatibility.");
    new(arena()) _::ReaderArena(this);
    allocatedArena = true;
  }

  _::SegmentReader* segment = arena()->tryGetSegment(_::SegmentId(0));
  KJ_REQUIRE(segment != nullptr &&
             segment->containsInterval(segment->getStartPtr(), segment->getStartPtr() + 1),
             "Message did not contain a root pointer.") {
    return _::StructReader();
  }

  return _::StructReader::readRoot(segment->getStartPtr(), segment, options.nestingLimit);
}

// -------------------------------------------------------------------

MessageBuilder::MessageBuilder(): allocatedArena(false) {}
MessageBuilder::~MessageBuilder() noexcept(false) {
  if (allocatedArena) {
    arena()->~BuilderArena();
  }
}

_::SegmentBuilder* MessageBuilder::getRootSegment() {
  if (allocatedArena) {
    return arena()->getSegment(_::SegmentId(0));
  } else {
    static_assert(sizeof(_::BuilderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a BuilderArena.  Please increase it.  This will break "
        "ABI compatibility.");
    new(arena()) _::BuilderArena(this);
    allocatedArena = true;

    WordCount ptrSize = 1 * POINTERS * WORDS_PER_POINTER;
    _::SegmentBuilder* segment = arena()->getSegmentWithAvailable(ptrSize);
    KJ_ASSERT(segment->getSegmentId() == _::SegmentId(0),
        "First allocated word of new arena was not in segment ID 0.");
    word* location = segment->allocate(ptrSize);
    KJ_ASSERT(location == segment->getPtrUnchecked(0 * WORDS),
        "First allocated word of new arena was not the first word in its segment.");
    return segment;
  }
}

_::StructBuilder MessageBuilder::initRoot(_::StructSize size) {
  _::SegmentBuilder* rootSegment = getRootSegment();
  return _::StructBuilder::initRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), size);
}

void MessageBuilder::setRootInternal(_::StructReader reader) {
  _::SegmentBuilder* rootSegment = getRootSegment();
  _::StructBuilder::setRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), reader);
}

_::StructBuilder MessageBuilder::getRoot(_::StructSize size) {
  _::SegmentBuilder* rootSegment = getRootSegment();
  return _::StructBuilder::getRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), size);
}

kj::ArrayPtr<const kj::ArrayPtr<const word>> MessageBuilder::getSegmentsForOutput() {
  if (allocatedArena) {
    return arena()->getSegmentsForOutput();
  } else {
    return nullptr;
  }
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
      for (void* ptr: s->segments) {
        free(ptr);
      }
    }
  }
}

kj::ArrayPtr<word> MallocMessageBuilder::allocateSegment(uint minimumSize) {
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

  uint size = std::max(minimumSize, nextSize);

  void* result = calloc(size, sizeof(word));
  if (result == nullptr) {
    FAIL_SYSCALL("calloc(size, sizeof(word))", ENOMEM, size);
  }

  if (!returnedFirstSegment) {
    firstSegment = result;
    returnedFirstSegment = true;

    // After the first segment, we want nextSize to equal the total size allocated so far.
    if (allocationStrategy == AllocationStrategy::GROW_HEURISTICALLY) nextSize = size;
  } else {
    MoreSegments* segments;
    KJ_IF_MAYBE(s, moreSegments) {
      segments = s;
    } else {
      auto newSegments = kj::heap<MoreSegments>();
      segments = newSegments;
      moreSegments = mv(newSegments);
    }
    segments->segments.push_back(result);
    if (allocationStrategy == AllocationStrategy::GROW_HEURISTICALLY) nextSize += size;
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
