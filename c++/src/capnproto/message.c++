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
#include "message.h"
#include "logging.h"
#include "arena.h"
#include "stdlib.h"
#include <exception>
#include <string>
#include <vector>
#include <unistd.h>

namespace capnproto {

MessageReader::MessageReader(ReaderOptions options): options(options), allocatedArena(false) {}
MessageReader::~MessageReader() {
  if (allocatedArena) {
    arena()->~ReaderArena();
  }
}

internal::StructReader MessageReader::getRootInternal() {
  if (!allocatedArena) {
    static_assert(sizeof(internal::ReaderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a ReaderArena.  Please increase it.  This will break "
        "ABI compatibility.");
    new(arena()) internal::ReaderArena(this);
    allocatedArena = true;
  }

  internal::SegmentReader* segment = arena()->tryGetSegment(internal::SegmentId(0));
  VALIDATE_INPUT(segment != nullptr &&
      segment->containsInterval(segment->getStartPtr(), segment->getStartPtr() + 1),
      "Message did not contain a root pointer.") {
    return internal::StructReader();
  }

  return internal::StructReader::readRoot(segment->getStartPtr(), segment, options.nestingLimit);
}

// -------------------------------------------------------------------

MessageBuilder::MessageBuilder(): allocatedArena(false) {}
MessageBuilder::~MessageBuilder() {
  if (allocatedArena) {
    arena()->~BuilderArena();
  }
}

internal::SegmentBuilder* MessageBuilder::getRootSegment() {
  if (allocatedArena) {
    return arena()->getSegment(internal::SegmentId(0));
  } else {
    static_assert(sizeof(internal::BuilderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a BuilderArena.  Please increase it.  This will break "
        "ABI compatibility.");
    new(arena()) internal::BuilderArena(this);
    allocatedArena = true;

    WordCount ptrSize = 1 * POINTERS * WORDS_PER_POINTER;
    internal::SegmentBuilder* segment = arena()->getSegmentWithAvailable(ptrSize);
    CHECK(segment->getSegmentId() == internal::SegmentId(0),
        "First allocated word of new arena was not in segment ID 0.");
    word* location = segment->allocate(ptrSize);
    CHECK(location == segment->getPtrUnchecked(0 * WORDS),
        "First allocated word of new arena was not the first word in its segment.");
    return segment;
  }
}

internal::StructBuilder MessageBuilder::initRoot(internal::StructSize size) {
  internal::SegmentBuilder* rootSegment = getRootSegment();
  return internal::StructBuilder::initRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), size);
}

void MessageBuilder::setRootInternal(internal::StructReader reader) {
  internal::SegmentBuilder* rootSegment = getRootSegment();
  internal::StructBuilder::setRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), reader);
}

internal::StructBuilder MessageBuilder::getRoot(internal::StructSize size) {
  internal::SegmentBuilder* rootSegment = getRootSegment();
  return internal::StructBuilder::getRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), size);
}

ArrayPtr<const ArrayPtr<const word>> MessageBuilder::getSegmentsForOutput() {
  if (allocatedArena) {
    return arena()->getSegmentsForOutput();
  } else {
    return nullptr;
  }
}

// =======================================================================================

SegmentArrayMessageReader::SegmentArrayMessageReader(
    ArrayPtr<const ArrayPtr<const word>> segments, ReaderOptions options)
    : MessageReader(options), segments(segments) {}

SegmentArrayMessageReader::~SegmentArrayMessageReader() {}

ArrayPtr<const word> SegmentArrayMessageReader::getSegment(uint id) {
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
    ArrayPtr<word> firstSegment, AllocationStrategy allocationStrategy)
    : nextSize(firstSegment.size()), allocationStrategy(allocationStrategy),
      ownFirstSegment(false), returnedFirstSegment(false), firstSegment(firstSegment.begin()) {
  PRECOND(firstSegment.size() > 0, "First segment size must be non-zero.");

  // Checking just the first word should catch most cases of failing to zero the segment.
  PRECOND(*reinterpret_cast<uint64_t*>(firstSegment.begin()) == 0,
          "First segment must be zeroed.");
}

MallocMessageBuilder::~MallocMessageBuilder() {
  if (returnedFirstSegment) {
    if (ownFirstSegment) {
      free(firstSegment);
    } else {
      // Must zero first segment.
      ArrayPtr<const ArrayPtr<const word>> segments = getSegmentsForOutput();
      if (segments.size() > 0) {
        CHECK(segments[0].begin() == firstSegment,
            "First segment in getSegmentsForOutput() is not the first segment allocated?");
        memset(firstSegment, 0, segments[0].size() * sizeof(word));
      }
    }

    if (moreSegments != nullptr) {
      for (void* ptr: moreSegments->segments) {
        free(ptr);
      }
    }
  }
}

ArrayPtr<word> MallocMessageBuilder::allocateSegment(uint minimumSize) {
  if (!returnedFirstSegment && !ownFirstSegment) {
    ArrayPtr<word> result = arrayPtr(reinterpret_cast<word*>(firstSegment), nextSize);
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
    if (moreSegments == nullptr) {
      moreSegments = std::unique_ptr<MoreSegments>(new MoreSegments);
    }
    moreSegments->segments.push_back(result);
    if (allocationStrategy == AllocationStrategy::GROW_HEURISTICALLY) nextSize += size;
  }

  return arrayPtr(reinterpret_cast<word*>(result), size);
}

// -------------------------------------------------------------------

FlatMessageBuilder::FlatMessageBuilder(ArrayPtr<word> array): array(array), allocated(false) {}
FlatMessageBuilder::~FlatMessageBuilder() {}

void FlatMessageBuilder::requireFilled() {
  PRECOND(getSegmentsForOutput()[0].end() == array.end(),
          "FlatMessageBuilder's buffer was too large.");
}

ArrayPtr<word> FlatMessageBuilder::allocateSegment(uint minimumSize) {
  PRECOND(!allocated, "FlatMessageBuilder's buffer was not large enough.");
  allocated = true;
  return array;
}

}  // namespace capnproto
