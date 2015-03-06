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

constexpr bool KJ_UNUSED isPowerOfTwo(size_t value) {
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
