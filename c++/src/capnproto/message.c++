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
#include <vector>
#include <string.h>
#include <iostream>
#include <stdlib.h>

namespace capnproto {

MessageReader::~MessageReader() {}
MessageBuilder::~MessageBuilder() {}

class MallocMessage: public MessageBuilder {
public:
  MallocMessage(WordCount preferredSegmentSize);
  ~MallocMessage();

  SegmentReader* tryGetSegment(SegmentId id);
  void reportInvalidData(const char* description);
  void reportReadLimitReached();
  SegmentBuilder* getSegment(SegmentId id);
  SegmentBuilder* getSegmentWithAvailable(WordCount minimumAvailable);

private:
  WordCount preferredSegmentSize;
  std::vector<std::unique_ptr<SegmentBuilder>> segments;
  std::vector<word*> memory;
};

MallocMessage::MallocMessage(WordCount preferredSegmentSize)
    : preferredSegmentSize(preferredSegmentSize) {}
MallocMessage::~MallocMessage() {
  for (word* ptr: memory) {
    free(ptr);
  }
}

SegmentReader* MallocMessage::tryGetSegment(SegmentId id) {
  if (id.value >= segments.size()) {
    return nullptr;
  } else {
    return segments[id.value].get();
  }
}

void MallocMessage::reportInvalidData(const char* description) {
  // TODO:  Better error reporting.
  std::cerr << "MallocMessage: Parse error: " << description << std::endl;
}

void MallocMessage::reportReadLimitReached() {
  // TODO:  Better error reporting.
  std::cerr << "MallocMessage: Exceeded read limit." << std::endl;
}

SegmentBuilder* MallocMessage::getSegment(SegmentId id) {
  return segments[id.value].get();
}

SegmentBuilder* MallocMessage::getSegmentWithAvailable(WordCount minimumAvailable) {
  if (segments.empty() || segments.back()->available() < minimumAvailable) {
    WordCount newSize = std::max(minimumAvailable, preferredSegmentSize);
    memory.push_back(reinterpret_cast<word*>(calloc(newSize / WORDS, sizeof(word))));
    segments.push_back(std::unique_ptr<SegmentBuilder>(new SegmentBuilder(
        this, SegmentId(segments.size()), memory.back(), newSize)));
  }
  return segments.back().get();
}

std::unique_ptr<MessageBuilder> newMallocMessage(WordCount preferredSegmentSize) {
  return std::unique_ptr<MessageBuilder>(new MallocMessage(preferredSegmentSize));
}

}  // namespace capnproto
