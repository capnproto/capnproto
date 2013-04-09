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

#include "message.h"
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

  internal::SegmentReader* segment = arena()->tryGetSegment(SegmentId(0));
  if (segment == nullptr ||
      !segment->containsInterval(segment->getStartPtr(), segment->getStartPtr() + 1)) {
    arena()->reportInvalidData("Message did not contain a root pointer.");
    return internal::StructReader::readEmpty();
  } else {
    return internal::StructReader::readRoot(segment->getStartPtr(), segment, options.nestingLimit);
  }
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
    return arena()->getSegment(SegmentId(0));
  } else {
    static_assert(sizeof(internal::BuilderArena) <= sizeof(arenaSpace),
        "arenaSpace is too small to hold a BuilderArena.  Please increase it.  This will break "
        "ABI compatibility.");
    new(arena()) internal::BuilderArena(this);
    allocatedArena = true;

    WordCount refSize = 1 * REFERENCES * WORDS_PER_REFERENCE;
    internal::SegmentBuilder* segment = arena()->getSegmentWithAvailable(refSize);
    CAPNPROTO_ASSERT(segment->getSegmentId() == SegmentId(0),
        "First allocated word of new arena was not in segment ID 0.");
    word* location = segment->allocate(refSize);
    CAPNPROTO_ASSERT(location == segment->getPtrUnchecked(0 * WORDS),
        "First allocated word of new arena was not the first word in its segment.");
    return segment;
  }
}

internal::StructBuilder MessageBuilder::initRoot(internal::StructSize size) {
  internal::SegmentBuilder* rootSegment = getRootSegment();
  return internal::StructBuilder::initRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), size);
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

ErrorReporter::~ErrorReporter() {}

class ParseException: public std::exception {
public:
  ParseException(std::string message)
      : message(message) {}
  ~ParseException() noexcept {}

  const char* what() const noexcept override {
    return message.c_str();
  }

private:
  std::string message;
};

class ThrowingErrorReporter: public ErrorReporter {
public:
  virtual ~ThrowingErrorReporter() {}

  void reportError(const char* description) override {
    std::string message("Cap'n Proto message was invalid: ");
    message += description;
    throw ParseException(std::move(message));
  }
};

ErrorReporter* getThrowingErrorReporter() {
  static ThrowingErrorReporter instance;
  return &instance;
}

class StderrErrorReporter: public ErrorReporter {
public:
  virtual ~StderrErrorReporter() {}

  void reportError(const char* description) override {
    std::string message("ERROR: Cap'n Proto message was invalid: ");
    message += description;
    message += '\n';
    write(STDERR_FILENO, message.data(), message.size());
  }
};

ErrorReporter* getStderrErrorReporter() {
  static StderrErrorReporter instance;
  return &instance;
}

class IgnoringErrorReporter: public ErrorReporter {
public:
  virtual ~IgnoringErrorReporter() {}

  void reportError(const char* description) override {}
};

ErrorReporter* getIgnoringErrorReporter() {
  static IgnoringErrorReporter instance;
  return &instance;
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
      ownFirstSegment(true), firstSegment(nullptr) {}

MallocMessageBuilder::MallocMessageBuilder(
    ArrayPtr<word> firstSegment, AllocationStrategy allocationStrategy)
    : nextSize(firstSegment.size()), allocationStrategy(allocationStrategy),
      ownFirstSegment(false), firstSegment(firstSegment.begin()) {}

MallocMessageBuilder::~MallocMessageBuilder() {
  if (ownFirstSegment) {
    free(firstSegment);
  } else {
    ArrayPtr<const ArrayPtr<const word>> segments = getSegmentsForOutput();
    if (segments.size() > 0) {
      CAPNPROTO_ASSERT(segments[0].begin() == firstSegment,
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

ArrayPtr<word> MallocMessageBuilder::allocateSegment(uint minimumSize) {
  if (!ownFirstSegment) {
    ArrayPtr<word> result = arrayPtr(reinterpret_cast<word*>(firstSegment), nextSize);
    firstSegment = nullptr;
    ownFirstSegment = true;
    if (result.size() >= minimumSize) {
      return result;
    }
    // If the provided first segment wasn't big enough, we discard it and proceed to allocate
    // our own.  This never happens in practice since minimumSize is always 1 for the first
    // segment.
  }

  uint size = std::max(minimumSize, nextSize);

  void* result = calloc(size, sizeof(word));
  if (result == nullptr) {
    throw std::bad_alloc();
  }

  if (firstSegment == nullptr) {
    firstSegment = result;
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

}  // namespace capnproto
