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

ReaderContext::~ReaderContext() {}
BuilderContext::~BuilderContext() {}

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

class DefaultReaderContext: public ReaderContext {
public:
  DefaultReaderContext(ArrayPtr<const ArrayPtr<const word>> segments,
      ErrorBehavior errorBehavior, uint64_t readLimit, uint nestingLimit)
      : segments(segments), errorBehavior(errorBehavior), readLimit(readLimit),
        nestingLimit(nestingLimit) {}
  ~DefaultReaderContext() {}

  ArrayPtr<const word> getSegment(uint id) override {
    if (id < segments.size()) {
      return segments[id];
    } else {
      return nullptr;
    }
  }

  uint64_t getReadLimit() override {
    return readLimit;
  }

  uint getNestingLimit() override {
    return nestingLimit;
  }

  void reportError(const char* description) override {
    std::string message("ERROR: Cap'n Proto parse error: ");
    message += description;
    message += '\n';

    switch (errorBehavior) {
      case ErrorBehavior::THROW_EXCEPTION:
        throw ParseException(std::move(message));
        break;
      case ErrorBehavior::REPORT_TO_STDERR_AND_RETURN_DEFAULT:
        write(STDERR_FILENO, message.data(), message.size());
        break;
      case ErrorBehavior::IGNORE_AND_RETURN_DEFAULT:
        break;
    }
  }

private:
  ArrayPtr<const ArrayPtr<const word>> segments;
  ErrorBehavior errorBehavior;
  uint64_t readLimit;
  uint nestingLimit;
};

std::unique_ptr<ReaderContext> newReaderContext(
    ArrayPtr<const ArrayPtr<const word>> segments,
    ErrorBehavior errorBehavior, uint64_t readLimit, uint nestingLimit) {
  return std::unique_ptr<ReaderContext>(new DefaultReaderContext(
      segments, errorBehavior, readLimit, nestingLimit));
}

class DefaultBuilderContext: public BuilderContext {
public:
  DefaultBuilderContext(uint firstSegmentWords, bool enableGrowthHeursitic)
      : nextSize(firstSegmentWords), enableGrowthHeursitic(enableGrowthHeursitic),
        firstSegment(nullptr) {}

  ~DefaultBuilderContext() {
    free(firstSegment);
    for (void* ptr: moreSegments) {
      free(ptr);
    }
  }

  ArrayPtr<word> allocateSegment(uint minimumSize) override {
    uint size = std::max(minimumSize, nextSize);

    void* result = calloc(size, sizeof(word));
    if (result == nullptr) {
      throw std::bad_alloc();
    }

    if (firstSegment == nullptr) {
      firstSegment = result;
      if (enableGrowthHeursitic) nextSize = size;
    } else {
      moreSegments.push_back(result);
      if (enableGrowthHeursitic) nextSize += size;
    }

    return arrayPtr(reinterpret_cast<word*>(result), size);
  }

private:
  uint nextSize;
  bool enableGrowthHeursitic;

  // Avoid allocating the vector if there is only one segment.
  void* firstSegment;
  std::vector<void*> moreSegments;
};

std::unique_ptr<BuilderContext> newBuilderContext(uint firstSegmentWords) {
  return std::unique_ptr<BuilderContext>(new DefaultBuilderContext(firstSegmentWords, true));
}

std::unique_ptr<BuilderContext> newFixedWidthBuilderContext(uint firstSegmentWords) {
  return std::unique_ptr<BuilderContext>(new DefaultBuilderContext(firstSegmentWords, false));
}

// =======================================================================================

namespace internal {

MessageImpl::Reader::Reader(ArrayPtr<const ArrayPtr<const word>> segments) {
  std::unique_ptr<ReaderContext> context = newReaderContext(segments);
  recursionLimit = context->getNestingLimit();

  static_assert(sizeof(ReaderArena) <= sizeof(arenaSpace),
      "arenaSpace is too small to hold a ReaderArena.  Please increase it.  This will break "
      "ABI compatibility.");
  new(arena()) ReaderArena(std::move(context));
}

MessageImpl::Reader::Reader(std::unique_ptr<ReaderContext> context)
    : recursionLimit(context->getNestingLimit()) {
  static_assert(sizeof(ReaderArena) <= sizeof(arenaSpace),
      "arenaSpace is too small to hold a ReaderArena.  Please increase it.  This will break "
      "ABI compatibility.");
  new(arena()) ReaderArena(std::move(context));
}

MessageImpl::Reader::~Reader() {
  arena()->~ReaderArena();
}

StructReader MessageImpl::Reader::getRoot(const word* defaultValue) {
  SegmentReader* segment = arena()->tryGetSegment(SegmentId(0));
  if (segment == nullptr ||
      !segment->containsInterval(segment->getStartPtr(), segment->getStartPtr() + 1)) {
    segment->getArena()->reportInvalidData("Message did not contain a root pointer.");
    return StructReader::readRootTrusted(defaultValue, defaultValue);
  } else {
    return StructReader::readRoot(segment->getStartPtr(), defaultValue, segment, recursionLimit);
  }
}

MessageImpl::Builder::Builder(): rootSegment(nullptr) {
  std::unique_ptr<BuilderContext> context = newBuilderContext();

  static_assert(sizeof(BuilderArena) <= sizeof(arenaSpace),
      "arenaSpace is too small to hold a BuilderArena.  Please increase it.  This will break "
      "ABI compatibility.");
  new(arena()) BuilderArena(std::move(context));
}

MessageImpl::Builder::Builder(std::unique_ptr<BuilderContext> context): rootSegment(nullptr) {
  static_assert(sizeof(BuilderArena) <= sizeof(arenaSpace),
      "arenaSpace is too small to hold a BuilderArena.  Please increase it.  This will break "
      "ABI compatibility.");
  new(arena()) BuilderArena(std::move(context));
}

MessageImpl::Builder::~Builder() {
  arena()->~BuilderArena();
}

StructBuilder MessageImpl::Builder::initRoot(const word* defaultValue) {
  if (rootSegment == nullptr) rootSegment = allocateRoot(arena());
  return StructBuilder::initRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), defaultValue);
}

StructBuilder MessageImpl::Builder::getRoot(const word* defaultValue) {
  if (rootSegment == nullptr) rootSegment = allocateRoot(arena());
  return StructBuilder::getRoot(rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), defaultValue);
}

ArrayPtr<const ArrayPtr<const word>> MessageImpl::Builder::getSegmentsForOutput() {
  return arena()->getSegmentsForOutput();
}

SegmentBuilder* MessageImpl::Builder::allocateRoot(BuilderArena* arena) {
  WordCount refSize = 1 * REFERENCES * WORDS_PER_REFERENCE;
  SegmentBuilder* segment = arena->getSegmentWithAvailable(refSize);
  CAPNPROTO_ASSERT(segment->getSegmentId() == SegmentId(0),
      "First allocated word of new arena was not in segment ID 0.");
  word* location = segment->allocate(refSize);
  CAPNPROTO_ASSERT(location == segment->getPtrUnchecked(0 * WORDS),
      "First allocated word of new arena was not the first word in its segment.");
  return segment;
}

}  // namespace internal
}  // namespace capnproto
