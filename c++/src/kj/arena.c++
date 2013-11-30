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

Arena::Arena(size_t chunkSizeHint): nextChunkSize(kj::max(sizeof(ChunkHeader), chunkSizeHint)) {}

Arena::Arena(ArrayPtr<byte> scratch)
    : nextChunkSize(kj::max(sizeof(ChunkHeader), scratch.size())) {
  if (scratch.size() > sizeof(ChunkHeader)) {
    ChunkHeader* chunk = reinterpret_cast<ChunkHeader*>(scratch.begin());
    chunk->end = scratch.end();
    chunk->pos = reinterpret_cast<byte*>(chunk + 1);
    chunk->next = nullptr;  // Never actually observed.

    // Don't place the chunk in the chunk list because it's not ours to delete.  Just make it the
    // current chunk so that we'll allocate from it until it is empty.
    currentChunk = chunk;
  }
}

Arena::~Arena() noexcept(false) {
  // Run cleanup() explicitly, but if it throws an exception, make sure to run it again as part of
  // unwind.  The second call will not throw because destructors are required to guard against
  // exceptions when already unwinding.
  KJ_ON_SCOPE_FAILURE(cleanup());
  cleanup();
}

void Arena::cleanup() {
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
  return (value & (value - 1)) == 0;
}

inline byte* alignTo(byte* p, uint alignment) {
  // Round the pointer up to the next aligned value.

  KJ_DASSERT(isPowerOfTwo(alignment), alignment);
  uintptr_t mask = alignment - 1;
  uintptr_t i = reinterpret_cast<uintptr_t>(p);
  return reinterpret_cast<byte*>((i + mask) & ~mask);
}

inline size_t alignTo(size_t s, uint alignment) {
  // Round the pointer up to the next aligned value.

  KJ_DASSERT(isPowerOfTwo(alignment), alignment);
  size_t mask = alignment - 1;
  return (s + mask) & ~mask;
}

}  // namespace

void* Arena::allocateBytes(size_t amount, uint alignment, bool hasDisposer) {
  if (hasDisposer) {
    alignment = kj::max(alignment, alignof(ObjectHeader));
    amount += alignTo(sizeof(ObjectHeader), alignment);
  }

  void* result = allocateBytesInternal(amount, alignment);

  if (hasDisposer) {
    // Reserve space for the ObjectHeader, but don't add it to the object list yet.
    result = alignTo(reinterpret_cast<byte*>(result) + sizeof(ObjectHeader), alignment);
  }

  KJ_DASSERT(reinterpret_cast<uintptr_t>(result) % alignment == 0);
  return result;
}

void* Arena::allocateBytesInternal(size_t amount, uint alignment) {
  if (currentChunk != nullptr) {
    ChunkHeader* chunk = currentChunk;
    byte* alignedPos = alignTo(chunk->pos, alignment);

    // Careful about overflow here.
    if (amount + (alignedPos - chunk->pos) <= chunk->end - chunk->pos) {
      // There's enough space in this chunk.
      chunk->pos = alignedPos + amount;
      return alignedPos;
    }
  }

  // Not enough space in the current chunk.  Allocate a new one.

  // We need to allocate at least enough space for the ChunkHeader and the requested allocation.

  // If the alignment is less than that of the chunk header, we'll need to increase it.
  alignment = kj::max(alignment, alignof(ChunkHeader));

  // If the ChunkHeader size does not match the alignment, we'll need to pad it up.
  amount += alignTo(sizeof(ChunkHeader), alignment);

  // Make sure we're going to allocate enough space.
  while (nextChunkSize < amount) {
    nextChunkSize *= 2;
  }

  // Allocate.
  byte* bytes = reinterpret_cast<byte*>(operator new(nextChunkSize));

  // Set up the ChunkHeader at the beginning of the allocation.
  ChunkHeader* newChunk = reinterpret_cast<ChunkHeader*>(bytes);
  newChunk->next = chunkList;
  newChunk->pos = bytes + amount;
  newChunk->end = bytes + nextChunkSize;
  currentChunk = newChunk;
  chunkList = newChunk;
  nextChunkSize *= 2;

  // Move past the ChunkHeader to find the position of the allocated object.
  return alignTo(bytes + sizeof(ChunkHeader), alignment);
}

StringPtr Arena::copyString(StringPtr content) {
  char* data = reinterpret_cast<char*>(allocateBytes(content.size() + 1, 1, false));
  memcpy(data, content.cStr(), content.size() + 1);
  return StringPtr(data, content.size());
}

void Arena::setDestructor(void* ptr, void (*destructor)(void*)) {
  ObjectHeader* header = reinterpret_cast<ObjectHeader*>(ptr) - 1;
  KJ_DASSERT(reinterpret_cast<uintptr_t>(header) % alignof(ObjectHeader) == 0);
  header->destructor = destructor;
  header->next = objectList;
  objectList = header;
}

}  // namespace kj
