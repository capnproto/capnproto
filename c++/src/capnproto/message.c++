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
#include <unistd.h>

namespace capnproto {

Allocator::~Allocator() {}
ErrorReporter::~ErrorReporter() {}

MallocAllocator::MallocAllocator(uint preferredSegmentSizeWords)
    : preferredSegmentSizeWords(preferredSegmentSizeWords) {}
MallocAllocator::~MallocAllocator() {}

MallocAllocator* MallocAllocator::getDefaultInstance() {
  static MallocAllocator defaultInstance(1024);
  return &defaultInstance;
}

ArrayPtr<word> MallocAllocator::allocate(SegmentId id, uint minimumSize) {
  uint size = std::max(minimumSize, preferredSegmentSizeWords);
  return arrayPtr(reinterpret_cast<word*>(calloc(size, sizeof(word))), size);
}
void MallocAllocator::free(SegmentId id, ArrayPtr<word> ptr) {
  ::free(ptr.begin());
}

StderrErrorReporter::~StderrErrorReporter() {}

StderrErrorReporter* StderrErrorReporter::getDefaultInstance() {
  static StderrErrorReporter defaultInstance;
  return &defaultInstance;
}

void StderrErrorReporter::reportError(const char* description) {
  std::string message("ERROR: Cap'n Proto parse error: ");
  message += description;
  message += '\n';
  write(STDERR_FILENO, message.data(), message.size());
}

class ParseException: public std::exception {
public:
  ParseException(const char* description);
  ~ParseException() noexcept;

  const char* what() const noexcept override;

private:
  std::string description;
};

ParseException::ParseException(const char* description)
    : description(description) {}

ParseException::~ParseException() noexcept {}

const char* ParseException::what() const noexcept {
  return description.c_str();
}

ThrowingErrorReporter::~ThrowingErrorReporter() {}

ThrowingErrorReporter* ThrowingErrorReporter::getDefaultInstance() {
  static ThrowingErrorReporter defaultInstance;
  return &defaultInstance;
}

void ThrowingErrorReporter::reportError(const char* description) {
  throw ParseException(description);
}

// =======================================================================================

namespace internal {

MessageImpl::Reader::Reader(ArrayPtr<const ArrayPtr<const word>> segments,
       uint recursionLimit, uint64_t readLimit, ErrorReporter* errorReporter)
    : arena(new ReaderArena(segments, errorReporter, readLimit * WORDS)),
      recursionLimit(recursionLimit) {}
MessageImpl::Reader::~Reader() {}

StructReader MessageImpl::Reader::getRoot(const word* defaultValue) {
  SegmentReader* segment = arena->tryGetSegment(SegmentId(0));
  if (segment == nullptr ||
      !segment->containsInterval(segment->getStartPtr(), segment->getStartPtr() + 1)) {
    segment->getArena()->reportInvalidData("Message did not contain a root pointer.");
    return StructReader::readRootTrusted(defaultValue, defaultValue);
  } else {
    return StructReader::readRoot(segment->getStartPtr(), defaultValue, segment, recursionLimit);
  }
}

MessageImpl::Builder::Builder()
    : arena(new BuilderArena(MallocAllocator::getDefaultInstance())),
      rootSegment(allocateRoot(arena.get())) {}
MessageImpl::Builder::Builder(Allocator* allocator)
    : arena(new BuilderArena(allocator)),
      rootSegment(allocateRoot(arena.get())) {}
MessageImpl::Builder::~Builder() {}

StructBuilder MessageImpl::Builder::initRoot(const word* defaultValue) {
  return StructBuilder::initRoot(
      rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), defaultValue);
}

StructBuilder MessageImpl::Builder::getRoot(const word* defaultValue) {
  return StructBuilder::getRoot(rootSegment, rootSegment->getPtrUnchecked(0 * WORDS), defaultValue);
}

ArrayPtr<const ArrayPtr<const word>> MessageImpl::Builder::getSegmentsForOutput() {
  return arena->getSegmentsForOutput();
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
