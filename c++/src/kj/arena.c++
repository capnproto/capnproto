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
#include "debug.h"
#include <stdint.h>

namespace kj {

const Arena::ArrayDisposerImpl Arena::ArrayDisposerImpl::instance = Arena::ArrayDisposerImpl();

Arena::Arena(size_t chunkSize): state(chunkSize) {}

Arena::Arena(ArrayPtr<byte> scratch, size_t chunkSize): state(chunkSize) {
  state.pos = scratch.begin();
  state.chunkEnd = scratch.end();
}

Arena::~Arena() noexcept(false) {
  // Run cleanup explicitly.  It will be executed again implicitly when state's destructor is
  // called.  This ensures that if the first pass throws an exception, remaining objects are still
  // destroyed.  If the second pass throws, the program terminates, but any destructors that could
  // throw should be using UnwindDetector to avoid this.
  state.cleanup();
}

void Arena::State::cleanup() {
  while (objectList != nullptr) {
    void* ptr = objectList + 1;
    auto destructor = objectList->destructor;
    objectList = objectList->next;
    destructor(ptr);
  }

  while (chunkList != nullptr) {
    void* ptr = chunkList;
    chunkList = chunkList->next;
    operator delete(ptr);
  }
}

namespace {

constexpr bool isPowerOfTwo(size_t value) {
  return (value & value - 1) == 0;
}

inline byte* alignTo(byte* p, uint alignment) {
  // Round the pointer up to the next aligned value.

  KJ_DASSERT(isPowerOfTwo(alignment), alignment);
  uintptr_t mask = alignment - 1;
  uintptr_t i = reinterpret_cast<uintptr_t>(p);
  return reinterpret_cast<byte*>((i + mask) & ~mask);
}

}  // namespace

void* Arena::allocateBytes(size_t amount, uint alignment, bool hasDisposer) {
  // Code below depends on power-of-two alignment and header sizes.
  static_assert(isPowerOfTwo(sizeof(ChunkHeader)), "sizeof(ChunkHeader) is not a power of 2.");
  static_assert(isPowerOfTwo(sizeof(ObjectHeader)), "sizeof(ObjectHeader) is not a power of 2.");
  KJ_DASSERT(isPowerOfTwo(alignment), alignment);

  // Offset we must apply if the allocated space is being prefixed with these headers.
  uint chunkHeaderSize = kj::max(alignment, sizeof(ChunkHeader));
  uint objectHeaderSize = kj::max(alignment, sizeof(ObjectHeader));

  if (hasDisposer) {
    amount += objectHeaderSize;
  }

  void* result;
  byte* alignedPos = alignTo(state.pos, alignment);
  byte* endPos = alignedPos + amount;
  if (endPos <= state.chunkEnd) {
    // There's enough space in the current chunk.
    result = alignedPos;
    state.pos = alignedPos + amount;
  } else if (amount + chunkHeaderSize > state.chunkSize ||
             state.chunkEnd - state.pos > state.chunkSize / 4) {
    // This object is too big to fit in one chunk, or we'd waste more than a quarter of the chunk
    // by starting a new one now.  Instead, allocate the object in its own independent chunk.
    ChunkHeader* newChunk = reinterpret_cast<ChunkHeader*>(operator new(chunkHeaderSize + amount));
    result = reinterpret_cast<byte*>(newChunk) + chunkHeaderSize;
    newChunk->next = state.chunkList;
    state.chunkList = newChunk;
    // We don't update state.pos and state.chunkEnd because the new chunk has no extra space but
    // the old chunk might.
  } else {
    // Allocate a new chunk.
    ChunkHeader* newChunk = reinterpret_cast<ChunkHeader*>(operator new(state.chunkSize));
    result = reinterpret_cast<byte*>(newChunk) + chunkHeaderSize;
    newChunk->next = state.chunkList;
    state.chunkList = newChunk;
    state.pos = reinterpret_cast<byte*>(result) + amount;
    state.chunkEnd = reinterpret_cast<byte*>(newChunk) + state.chunkSize;
  }

  if (hasDisposer) {
    // Reserve space for the ObjectHeader, but don't add it to the object list yet.
    result = reinterpret_cast<byte*>(result) + objectHeaderSize;
  }
  return result;
}

StringPtr Arena::copyString(StringPtr content) {
  char* data = reinterpret_cast<char*>(allocateBytes(content.size() + 1, 1, false));
  memcpy(data, content.cStr(), content.size() + 1);
  return StringPtr(data, content.size());
}

void Arena::setDestructor(void* ptr, void (*destructor)(void*)) {
  ObjectHeader* header = reinterpret_cast<ObjectHeader*>(ptr) - 1;
  header->destructor = destructor;
  header->next = state.objectList;
  state.objectList = header;
}

void Arena::ArrayDisposerImpl::disposeImpl(
    void* firstElement, size_t elementSize, size_t elementCount,
    size_t capacity, void (*destroyElement)(void*)) const {
  if (destroyElement != nullptr) {
    ExceptionSafeArrayUtil guard(firstElement, elementSize, elementCount, destroyElement);
    guard.destroyAll();
  }
}

}  // namespace kj
