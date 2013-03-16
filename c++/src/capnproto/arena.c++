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

ReaderArena::ReaderArena(
    ArrayPtr<const ArrayPtr<const word>> segments,
    ErrorReporter* errorReporter,
    WordCount64 readLimit)
    : segments(segments),
      errorReporter(errorReporter),
      readLimiter(readLimit) {
  segmentReaders.reserve(segments.size());
  uint i = 0;
  for (auto segment: segments) {
    segmentReaders.emplace_back(new SegmentReader(this, SegmentId(i++), segment, &readLimiter));
  }
}

ReaderArena::~ReaderArena() {}

ArrayPtr<const ArrayPtr<const word>> ReaderArena::getSegmentsForOutput() {
  return segments;
}

SegmentReader* ReaderArena::tryGetSegment(SegmentId id) {
  if (id.value >= segments.size()) {
    return nullptr;
  } else {
    return segmentReaders[id.value].get();
  }
}

void ReaderArena::reportInvalidData(const char* description) {
  errorReporter->reportError(description);
}

void ReaderArena::reportReadLimitReached() {
  errorReporter->reportError("Exceeded read limit.");
}

// =======================================================================================

BuilderArena::BuilderArena(Allocator* allocator): allocator(allocator) {}
BuilderArena::~BuilderArena() {
  // TODO:  This is wrong because we aren't taking into account how much of each segment is actually
  //   allocated.
  uint i = 0;
  for (ArrayPtr<word> ptr: memory) {
    // The memory array contains Array<const word> only to ease implementation of getSegmentsForOutput().
    // We actually own this space and can de-constify it.
    allocator->free(SegmentId(i++), ptr);
  }
}

SegmentBuilder* BuilderArena::getSegment(SegmentId id) {
  return segments[id.value].get();
}

SegmentBuilder* BuilderArena::getSegmentWithAvailable(WordCount minimumAvailable) {
  if (segments.empty() || segments.back()->available() < minimumAvailable) {
    ArrayPtr<word> array = allocator->allocate(
        SegmentId(segments.size()), minimumAvailable / WORDS);
    memory.push_back(array);
    segments.push_back(std::unique_ptr<SegmentBuilder>(new SegmentBuilder(
        this, SegmentId(segments.size()), array, &dummyLimiter)));
  }
  return segments.back().get();
}

ArrayPtr<const ArrayPtr<const word>> BuilderArena::getSegmentsForOutput() {
  segmentsForOutput.resize(segments.size());
  for (uint i = 0; i < segments.size(); i++) {
    segmentsForOutput[i] = segments[i]->currentlyAllocated();
  }

  return arrayPtr(&*segmentsForOutput.begin(), segmentsForOutput.size());
}

SegmentReader* BuilderArena::tryGetSegment(SegmentId id) {
  if (id.value >= segments.size()) {
    return nullptr;
  } else {
    return segments[id.value].get();
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
