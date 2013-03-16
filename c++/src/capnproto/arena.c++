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

#include "arena.h"
#include "message.h"
#include <vector>
#include <string.h>
#include <iostream>

namespace capnproto {
namespace internal {

Arena::~Arena() {}

// =======================================================================================

ReaderArena::ReaderArena(std::unique_ptr<ReaderContext> context)
    : context(std::move(context)),
      readLimiter(this->context->getReadLimit() * WORDS),
      segment0(this, SegmentId(0), this->context->getSegment(0), &readLimiter) {}

ReaderArena::~ReaderArena() {}

SegmentReader* ReaderArena::tryGetSegment(SegmentId id) {
  if (id == SegmentId(0)) {
    if (segment0.getArray() == nullptr) {
      return nullptr;
    } else {
      return &segment0;
    }
  }

  // TODO:  Lock a mutex so that reading is thread-safe.  Take a reader lock during the first
  //   lookup, unlock it before calling getSegment(), then take a writer lock to update the map.
  //   Bleh, lazy initialization is sad.

  if (moreSegments != nullptr) {
    auto iter = moreSegments->find(id.value);
    if (iter != moreSegments->end()) {
      return iter->second.get();
    }
  }

  ArrayPtr<const word> newSegment = context->getSegment(id.value);
  if (newSegment == nullptr) {
    return nullptr;
  }

  if (moreSegments == nullptr) {
    // OK, the segment exists, so allocate the map.
    moreSegments = std::unique_ptr<SegmentMap>(new SegmentMap);
  }

  std::unique_ptr<SegmentReader>* slot = &(*moreSegments)[id.value];
  *slot = std::unique_ptr<SegmentReader>(new SegmentReader(this, id, newSegment, &readLimiter));
  return slot->get();
}

void ReaderArena::reportInvalidData(const char* description) {
  context->reportError(description);
}

void ReaderArena::reportReadLimitReached() {
  context->reportError("Exceeded read limit.");
}

// =======================================================================================

BuilderArena::BuilderArena(std::unique_ptr<BuilderContext> context)
    : context(std::move(context)), segment0(nullptr, SegmentId(0), nullptr, nullptr) {}
BuilderArena::~BuilderArena() {}

SegmentBuilder* BuilderArena::getSegment(SegmentId id) {
  // This method is allowed to crash if the segment ID is not valid.
  if (id == SegmentId(0)) {
    return &segment0;
  } else {
    return moreSegments->builders[id.value - 1].get();
  }
}

SegmentBuilder* BuilderArena::getSegmentWithAvailable(WordCount minimumAvailable) {
  // TODO:  Mutex-locking?  Do we want to allow people to build different parts of the same message
  // in different threads?

  if (segment0.getArena() == nullptr) {
    // We're allocating the first segment.
    ArrayPtr<word> ptr = context->allocateSegment(minimumAvailable / WORDS);

    // Re-allocate segment0 in-place.  This is a bit of a hack, but we have not returned any
    // pointers to this segment yet, so it should be fine.
    segment0.~SegmentBuilder();
    return new (&segment0) SegmentBuilder(this, SegmentId(0), ptr, &this->dummyLimiter);
  } else {
    if (segment0.available() >= minimumAvailable) {
      return &segment0;
    }

    if (moreSegments == nullptr) {
      moreSegments = std::unique_ptr<MultiSegmentState>(new MultiSegmentState());
    } else {
      // TODO:  Check for available space in more than just the last segment.  We don't want this
      //   to be O(n), though, so we'll need to maintain some sort of table.  Complicating matters,
      //   we want SegmentBuilders::allocate() to be fast, so we can't update any such table when
      //   allocation actually happens.  Instead, we could have a priority queue based on the
      //   last-known available size, and then re-check the size when we pop segments off it and
      //   shove them to the back of the queue if they have become too small.
      if (moreSegments->builders.back()->available() >= minimumAvailable) {
        return moreSegments->builders.back().get();
      }
    }

    std::unique_ptr<SegmentBuilder> newBuilder = std::unique_ptr<SegmentBuilder>(
        new SegmentBuilder(this, SegmentId(moreSegments->builders.size() + 1),
            context->allocateSegment(minimumAvailable / WORDS), &this->dummyLimiter));
    SegmentBuilder* result = newBuilder.get();
    moreSegments->builders.push_back(std::move(newBuilder));

    // Keep forOutput the right size so that we don't have to re-allocate during
    // getSegmentsForOutput(), which callers might reasonably expect is a thread-safe method.
    moreSegments->forOutput.resize(moreSegments->builders.size() + 1);

    return result;
  }
}

ArrayPtr<const ArrayPtr<const word>> BuilderArena::getSegmentsForOutput() {
  // We shouldn't need to lock a mutex here because if this is called multiple times simultaneously,
  // we should only be overwriting the array with the exact same data.  If the number or size of
  // segments is actually changing due to an activity in another thread, then the caller has a
  // problem regardless of locking here.

  if (moreSegments == nullptr) {
    if (segment0.getArena() == nullptr) {
      // We haven't actually allocated any segments yet.
      return nullptr;
    } else {
      // We have only one segment so far.
      segment0ForOutput = segment0.currentlyAllocated();
      return arrayPtr(&segment0ForOutput, 1);
    }
  } else {
    CAPNPROTO_DEBUG_ASSERT(moreSegments->forOutput.size() == moreSegments->builders.size() + 1,
        "Bug in capnproto::internal::BuilderArena:  moreSegments->forOutput wasn't resized "
        "correctly when the last builder was added.");

    ArrayPtr<ArrayPtr<const word>> result(
        &moreSegments->forOutput[0], moreSegments->forOutput.size());
    uint i = 0;
    result[i++] = segment0.currentlyAllocated();
    for (auto& builder: moreSegments->builders) {
      result[i++] = builder->currentlyAllocated();
    }
    return result;
  }
}

SegmentReader* BuilderArena::tryGetSegment(SegmentId id) {
  if (id == SegmentId(0)) {
    if (segment0.getArena() == nullptr) {
      // We haven't allocated any segments yet.
      return nullptr;
    } else {
      return &segment0;
    }
  } else {
    if (moreSegments == nullptr || id.value > moreSegments->builders.size()) {
      return nullptr;
    } else {
      return moreSegments->builders[id.value - 1].get();
    }
  }
}

void BuilderArena::reportInvalidData(const char* description) {
  // TODO:  Better error reporting.
  std::cerr << "BuilderArena: Parse error: " << description << std::endl;
}

void BuilderArena::reportReadLimitReached() {
  // TODO:  Better error reporting.
  std::cerr << "BuilderArena: Exceeded read limit." << std::endl;
}

}  // namespace internal
}  // namespace capnproto
