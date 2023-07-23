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
#include "arena.h"
#include "message.h"
#include <kj/debug.h>
#include <kj/refcount.h>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if !CAPNP_LITE
#include "capability.h"
#endif  // !CAPNP_LITE

namespace capnp {
namespace _ {  // private

Arena::~Arena() noexcept(false) {}

void ReadLimiter::unread(WordCount64 amount) {
  // Be careful not to overflow here.  Since ReadLimiter has no thread-safety, it's possible that
  // the limit value was not updated correctly for one or more reads, and therefore unread() could
  // overflow it even if it is only unreading bytes that were actually read.
  uint64_t oldValue = limit;
  uint64_t newValue = oldValue + unbound(amount / WORDS);
  if (newValue > oldValue) {
    limit = newValue;
  }
}

void SegmentReader::abortCheckObjectFault() {
  KJ_LOG(FATAL, "checkObject()'s parameter is not in-range; this would segfault in opt mode",
                "this is a serious bug in Cap'n Proto; please notify security@sandstorm.io");
  abort();
}

void SegmentBuilder::throwNotWritable() {
  KJ_FAIL_REQUIRE(
      "Tried to form a Builder to an external data segment referenced by the MessageBuilder.  "
      "When you use Orphanage::reference*(), you are not allowed to obtain Builders to the "
      "referenced data, only Readers, because that data is const.");
}

// =======================================================================================

static SegmentWordCount verifySegmentSize(size_t size) {
  auto gsize = bounded(size) * WORDS;
  return assertMaxBits<SEGMENT_WORD_COUNT_BITS>(gsize, [&]() {
    KJ_FAIL_REQUIRE("segment is too large", size);
  });
}

inline ReaderArena::ReaderArena(MessageReader* message, const word* firstSegment,
                                SegmentWordCount firstSegmentSize)
    : message(message),
      readLimiter(bounded(message->getOptions().traversalLimitInWords) * WORDS),
      segment0(this, SegmentId(0), firstSegment, firstSegmentSize, &readLimiter) {}

inline ReaderArena::ReaderArena(MessageReader* message, kj::ArrayPtr<const word> firstSegment)
    : ReaderArena(message, firstSegment.begin(), verifySegmentSize(firstSegment.size())) {}

ReaderArena::ReaderArena(MessageReader* message)
    : ReaderArena(message, message->getSegment(0)) {}

ReaderArena::~ReaderArena() noexcept(false) {}

size_t ReaderArena::sizeInWords() {
  size_t total = segment0.getArray().size();

  for (uint i = 0; ; i++) {
    SegmentReader* segment = tryGetSegment(SegmentId(i));
    if (segment == nullptr) return total;
    total += unboundAs<size_t>(segment->getSize() / WORDS);
  }
}

SegmentReader* ReaderArena::tryGetSegment(SegmentId id) {
  if (id == SegmentId(0)) {
    if (segment0.getArray() == nullptr) {
      return nullptr;
    } else {
      return &segment0;
    }
  }

  auto lock = moreSegments.lockExclusive();

  SegmentMap* segments = nullptr;
  KJ_IF_MAYBE(s, *lock) {
    KJ_IF_MAYBE(segment, s->find(id.value)) {
      return *segment;
    }
    segments = s;
  }

  kj::ArrayPtr<const word> newSegment = message->getSegment(id.value);
  if (newSegment == nullptr) {
    return nullptr;
  }

  SegmentWordCount newSegmentSize = verifySegmentSize(newSegment.size());

  if (*lock == nullptr) {
    // OK, the segment exists, so allocate the map.
    segments = &lock->emplace();
  }

  auto segment = kj::heap<SegmentReader>(
      this, id, newSegment.begin(), newSegmentSize, &readLimiter);
  SegmentReader* result = segment;
  segments->insert(id.value, kj::mv(segment));
  return result;
}

void ReaderArena::reportReadLimitReached() {
  KJ_FAIL_REQUIRE("Exceeded message traversal limit.  See capnp::ReaderOptions.") {
    return;
  }
}

// =======================================================================================

BuilderArena::BuilderArena(MessageBuilder* message)
    : message(message), segment0(nullptr, SegmentId(0), nullptr, nullptr) {}

BuilderArena::BuilderArena(MessageBuilder* message,
                           kj::ArrayPtr<MessageBuilder::SegmentInit> segments)
    : message(message),
      segment0(this, SegmentId(0), segments[0].space.begin(),
               verifySegmentSize(segments[0].space.size()),
               &this->dummyLimiter, verifySegmentSize(segments[0].wordsUsed)) {
  if (segments.size() > 1) {
    kj::Vector<kj::Own<SegmentBuilder>> builders(segments.size() - 1);

    uint i = 1;
    for (auto& segment: segments.slice(1, segments.size())) {
      builders.add(kj::heap<SegmentBuilder>(
          this, SegmentId(i++), segment.space.begin(), verifySegmentSize(segment.space.size()),
          &this->dummyLimiter, verifySegmentSize(segment.wordsUsed)));
    }

    kj::Vector<kj::ArrayPtr<const word>> forOutput;
    forOutput.resize(segments.size());

    segmentWithSpace = builders.back();

    this->moreSegments = kj::heap<MultiSegmentState>(
        MultiSegmentState { kj::mv(builders), kj::mv(forOutput) });

  } else {
    segmentWithSpace = &segment0;
  }
}

BuilderArena::~BuilderArena() noexcept(false) {}

size_t BuilderArena::sizeInWords() {
  KJ_IF_MAYBE(segmentState, moreSegments) {
    size_t total = segment0.currentlyAllocated().size();
    for (auto& builder: segmentState->get()->builders) {
      total += builder->currentlyAllocated().size();
    }
    return total;
  } else {
    if (segment0.getArena() == nullptr) {
      // We haven't actually allocated any segments yet.
      return 0;
    } else {
      // We have only one segment so far.
      return segment0.currentlyAllocated().size();
    }
  }
}

SegmentBuilder* BuilderArena::getSegment(SegmentId id) {
  // This method is allowed to fail if the segment ID is not valid.
  if (id == SegmentId(0)) {
    return &segment0;
  } else {
    KJ_IF_MAYBE(s, moreSegments) {
      KJ_REQUIRE(id.value - 1 < s->get()->builders.size(), "invalid segment id", id.value);
      return const_cast<SegmentBuilder*>(s->get()->builders[id.value - 1].get());
    } else {
      KJ_FAIL_REQUIRE("invalid segment id", id.value);
    }
  }
}

BuilderArena::AllocateResult BuilderArena::allocate(SegmentWordCount amount) {
  if (segment0.getArena() == nullptr) {
    // We're allocating the first segment.
    kj::ArrayPtr<word> ptr = message->allocateSegment(unbound(amount / WORDS));
    auto actualSize = verifySegmentSize(ptr.size());

    // Re-allocate segment0 in-place.  This is a bit of a hack, but we have not returned any
    // pointers to this segment yet, so it should be fine.
    kj::dtor(segment0);
    kj::ctor(segment0, this, SegmentId(0), ptr.begin(), actualSize, &this->dummyLimiter);

    segmentWithSpace = &segment0;
    return AllocateResult { &segment0, segment0.allocate(amount) };
  } else {
    if (segmentWithSpace != nullptr) {
      // Check if there is space in an existing segment.
      // TODO(perf):  Check for available space in more than just the last segment.  We don't
      //   want this to be O(n), though, so we'll need to maintain some sort of table.  Complicating
      //   matters, we want SegmentBuilders::allocate() to be fast, so we can't update any such
      //   table when allocation actually happens.  Instead, we could have a priority queue based
      //   on the last-known available size, and then re-check the size when we pop segments off it
      //   and shove them to the back of the queue if they have become too small.
      word* attempt = segmentWithSpace->allocate(amount);
      if (attempt != nullptr) {
        return AllocateResult { segmentWithSpace, attempt };
      }
    }

    // Need to allocate a new segment.
    SegmentBuilder* result = addSegmentInternal(message->allocateSegment(unbound(amount / WORDS)));

    // Check this new segment first the next time we need to allocate.
    segmentWithSpace = result;

    // Allocating from the new segment is guaranteed to succeed since we made it big enough.
    return AllocateResult { result, result->allocate(amount) };
  }
}

SegmentBuilder* BuilderArena::addExternalSegment(kj::ArrayPtr<const word> content) {
  return addSegmentInternal(content);
}

template <typename T>
SegmentBuilder* BuilderArena::addSegmentInternal(kj::ArrayPtr<T> content) {
  // This check should never fail in practice, since you can't get an Orphanage without allocating
  // the root segment.
  KJ_REQUIRE(segment0.getArena() != nullptr,
      "Can't allocate external segments before allocating the root segment.");

  auto contentSize = verifySegmentSize(content.size());

  MultiSegmentState* segmentState;
  KJ_IF_MAYBE(s, moreSegments) {
    segmentState = *s;
  } else {
    auto newSegmentState = kj::heap<MultiSegmentState>();
    segmentState = newSegmentState;
    moreSegments = kj::mv(newSegmentState);
  }

  kj::Own<SegmentBuilder> newBuilder = kj::heap<SegmentBuilder>(
      this, SegmentId(segmentState->builders.size() + 1),
      content.begin(), contentSize, &this->dummyLimiter);
  SegmentBuilder* result = newBuilder.get();
  segmentState->builders.add(kj::mv(newBuilder));

  // Keep forOutput the right size so that we don't have to re-allocate during
  // getSegmentsForOutput(), which callers might reasonably expect is a thread-safe method.
  segmentState->forOutput.resize(segmentState->builders.size() + 1);

  return result;
}

kj::ArrayPtr<const kj::ArrayPtr<const word>> BuilderArena::getSegmentsForOutput() {
  // Although this is a read-only method, we shouldn't need to lock a mutex here because if this
  // is called multiple times simultaneously, we should only be overwriting the array with the
  // exact same data.  If the number or size of segments is actually changing due to an activity
  // in another thread, then the caller has a problem regardless of locking here.

  KJ_IF_MAYBE(segmentState, moreSegments) {
    KJ_DASSERT(segmentState->get()->forOutput.size() == segmentState->get()->builders.size() + 1,
        "segmentState->forOutput wasn't resized correctly when the last builder was added.",
        segmentState->get()->forOutput.size(), segmentState->get()->builders.size());

    kj::ArrayPtr<kj::ArrayPtr<const word>> result(
        &segmentState->get()->forOutput[0], segmentState->get()->forOutput.size());
    uint i = 0;
    result[i++] = segment0.currentlyAllocated();
    for (auto& builder: segmentState->get()->builders) {
      result[i++] = builder->currentlyAllocated();
    }
    return result;
  } else {
    if (segment0.getArena() == nullptr) {
      // We haven't actually allocated any segments yet.
      return nullptr;
    } else {
      // We have only one segment so far.
      segment0ForOutput = segment0.currentlyAllocated();
      return kj::arrayPtr(&segment0ForOutput, 1);
    }
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
    KJ_IF_MAYBE(segmentState, moreSegments) {
      if (id.value <= segmentState->get()->builders.size()) {
        // TODO(cleanup):  Return a const SegmentReader and tediously constify all SegmentBuilder
        //   pointers throughout the codebase.
        return const_cast<SegmentReader*>(kj::implicitCast<const SegmentReader*>(
            segmentState->get()->builders[id.value - 1].get()));
      }
    }
    return nullptr;
  }
}

void BuilderArena::reportReadLimitReached() {
  KJ_FAIL_ASSERT("Read limit reached for BuilderArena, but it should have been unlimited.") {
    return;
  }
}

#if !CAPNP_LITE
kj::Maybe<kj::Own<ClientHook>> BuilderArena::LocalCapTable::extractCap(uint index) {
  if (index < capTable.size()) {
    return capTable[index].map([](kj::Own<ClientHook>& cap) { return cap->addRef(); });
  } else {
    return nullptr;
  }
}

uint BuilderArena::LocalCapTable::injectCap(kj::Own<ClientHook>&& cap) {
  uint result = capTable.size();
  capTable.add(kj::mv(cap));
  return result;
}

void BuilderArena::LocalCapTable::dropCap(uint index) {
  KJ_ASSERT(index < capTable.size(), "Invalid capability descriptor in message.") {
    return;
  }
  capTable[index] = nullptr;
}
#endif  // !CAPNP_LITE

}  // namespace _ (private)
}  // namespace capnp
