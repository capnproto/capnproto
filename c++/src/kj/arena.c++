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

Arena::Arena(size_t chunkSizeHint): state(kj::max(sizeof(ChunkHeader), chunkSizeHint)) {}

Arena::Arena(ArrayPtr<byte> scratch)
    : state(kj::max(sizeof(ChunkHeader), scratch.size())) {
  if (scratch.size() > sizeof(ChunkHeader)) {
    ChunkHeader* chunk = reinterpret_cast<ChunkHeader*>(scratch.begin());
    chunk->end = scratch.end();
    chunk->pos = reinterpret_cast<byte*>(chunk + 1);
    chunk->next = nullptr;  // Never actually observed.

    // Don't place the chunk in the chunk list because it's not ours to delete.  Just make it the
    // current chunk so that we'll allocate from it until it is empty.
    state.getWithoutLock().currentChunk = chunk;
  }
}

Arena::~Arena() noexcept(false) {
  // Run cleanup explicitly.  It will be executed again implicitly when state's destructor is
  // called.  This ensures that if the first pass throws an exception, remaining objects are still
  // destroyed.  If the second pass throws, the program terminates, but any destructors that could
  // throw should be using UnwindDetector to avoid this.
  state.getWithoutLock().cleanup();
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

void* Arena::allocateBytes(size_t amount, uint alignment, bool hasDisposer) const {
  if (hasDisposer) {
    alignment = kj::max(alignment, alignof(ObjectHeader));
    amount += alignTo(sizeof(ObjectHeader), alignment);
  }

  void* result = allocateBytesLockless(amount, alignment);

  if (result == nullptr) {
    result = allocateBytesFallback(amount, alignment);
  }

  if (hasDisposer) {
    // Reserve space for the ObjectHeader, but don't add it to the object list yet.
    result = alignTo(reinterpret_cast<byte*>(result) + sizeof(ObjectHeader), alignment);
  }

  KJ_DASSERT(reinterpret_cast<uintptr_t>(result) % alignment == 0);
  return result;
}

void* Arena::allocateBytesLockless(size_t amount, uint alignment) const {
  for (;;) {
    ChunkHeader* chunk = __atomic_load_n(&state.getWithoutLock().currentChunk, __ATOMIC_ACQUIRE);

    if (chunk == nullptr) {
      // No chunks allocated yet.
      return nullptr;
    }

    byte* pos = __atomic_load_n(&chunk->pos, __ATOMIC_RELAXED);
    byte* alignedPos = alignTo(pos, alignment);
    byte* endPos = alignedPos + amount;

    // Careful about pointer wrapping (e.g. if the chunk is near the end of the address space).
    if (chunk->end - endPos < 0) {
      // Not enough space.
      return nullptr;
    }

    // There appears to be enough space in this chunk, unless another thread stole it.
    if (KJ_LIKELY(__atomic_compare_exchange_n(
          &chunk->pos, &pos, endPos, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))) {
      return alignedPos;
    }

    // Contention.  Retry.
  }
}

void* Arena::allocateBytesFallback(size_t amount, uint alignment) const {
  auto lock = state.lockExclusive();

  // Now that we have the lock, try one more time to allocate from the current chunk.  This could
  // work if another thread allocated a new chunk while we were waiting for the lock.
  void* locklessResult = allocateBytesLockless(amount, alignment);
  if (locklessResult != nullptr) {
    return locklessResult;
  }

  // OK, we know the current chunk is out of space and we hold the lock so no one else is
  // allocating a new one.  Let's do it!

  alignment = kj::max(alignment, alignof(ChunkHeader));
  amount += alignTo(sizeof(ChunkHeader), alignment);

  while (lock->nextChunkSize < amount) {
    lock->nextChunkSize *= 2;
  }

  byte* bytes = reinterpret_cast<byte*>(operator new(lock->nextChunkSize));

  ChunkHeader* newChunk = reinterpret_cast<ChunkHeader*>(bytes);
  newChunk->next = lock->chunkList;
  newChunk->pos = bytes + amount;
  newChunk->end = bytes + lock->nextChunkSize;
  __atomic_store_n(&lock->currentChunk, newChunk, __ATOMIC_RELEASE);

  lock->nextChunkSize *= 2;

  byte* result = alignTo(bytes + sizeof(ChunkHeader), alignment);
  lock->chunkList = newChunk;

  return result;
}

StringPtr Arena::copyString(StringPtr content) const {
  char* data = reinterpret_cast<char*>(allocateBytes(content.size() + 1, 1, false));
  memcpy(data, content.cStr(), content.size() + 1);
  return StringPtr(data, content.size());
}

void Arena::setDestructor(void* ptr, void (*destructor)(void*)) const {
  ObjectHeader* header = reinterpret_cast<ObjectHeader*>(ptr) - 1;
  KJ_DASSERT(reinterpret_cast<uintptr_t>(header) % alignof(ObjectHeader) == 0);
  header->destructor = destructor;
  header->next = state.getWithoutLock().objectList;

  // We can use relaxed atomics here because the object list is not actually traversed until the
  // destructor, which needs to be synchronized in its own way.
  while (!__atomic_compare_exchange_n(
      &state.getWithoutLock().objectList, &header->next, header, true,
      __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    // Retry.
  }
}

}  // namespace kj
