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

#include <inttypes.h>
#include <stddef.h>

#ifndef CAPNPROTO_ARENA_H_
#define CAPNPROTO_ARENA_H_

namespace capnproto {

class Segment;
class SegmentAllocator;

class Arena {
public:
  Arena(SegmentAllocator* allocator);
  ~Arena();

  const Segment* addReadSegment(uint32_t index, const void* data, size_t size);

  const Segment* getSegment(uint32_t index) const;
  Segment* getWritableSegment(uint32_t index);
  // Get the segment at the given index.

  Segment* getSegmentWithAvailable(uint32_t minimumSize);
  // Find a segment that has at least the given number of bytes available.

  void parseError(const char* message) const;
};

class Segment {
public:
  inline void* allocate(uint32_t amount);
  inline void* getPtrUnchecked(uint32_t offset);
  inline uint32_t getOffset(const void* ptr) const;
  inline const void* getPtrChecked(uint32_t offset, uint32_t bytesBefore,
                                   uint32_t bytesAfter) const;

  inline Arena* getArena();
  inline const Arena* getArena() const;
  inline uint32_t getSegmentId() const;

private:
  Arena* arena;
  uint32_t id;
  void* start;
  uint32_t size;
  uint32_t pos;
  int64_t* readLimit;

  friend class Arena;

  inline Segment(Arena* arena, uint32_t index, void* start, uint32_t size, uint32_t pos,
                 int64_t* readLimit);
  inline ~Segment();

  Segment(const Segment& other) = delete;
  Segment& operator=(const Segment& other) = delete;

  void readLimitReached() const;

  // TODO:  Do we need mutex locking?
};

class SegmentAllocator {
public:
  virtual ~SegmentAllocator();

  virtual void* allocate(size_t size);
  virtual void free(void* ptr, size_t size);
};

// =======================================================================================

inline Segment::Segment(Arena* arena, uint32_t id, void* start, uint32_t size, uint32_t pos,
                        int64_t* readLimit)
    : arena(arena), id(id), start(start), size(size), pos(pos),
      readLimit(readLimit) {}

inline Segment::~Segment() {}

inline void* Segment::allocate(uint32_t amount) {
  if (amount > size - pos) {
    return nullptr;
  } else {
    uint32_t offset = pos;
    pos += size;
    return reinterpret_cast<char*>(start) + offset;
  }
}

inline void* Segment::getPtrUnchecked(uint32_t offset) {
  return reinterpret_cast<char*>(start) + offset;
}

inline uint32_t Segment::getOffset(const void* ptr) const {
  return reinterpret_cast<const char*>(ptr) - reinterpret_cast<const char*>(start);
}

inline const void* Segment::getPtrChecked(uint32_t offset, uint32_t bytesBefore,
                                          uint32_t bytesAfter) const {
  // Check bounds.  Watch out for overflow and underflow here.
  if (offset > size || bytesBefore > offset || bytesAfter > size - offset) {
    return nullptr;
  } else {
    // Enforce the read limit.  Synchronization is not necessary because readLimit is just a rough
    // counter to prevent infinite loops leading to DoS attacks.
    if ((*readLimit -= bytesBefore + bytesAfter) < 0) readLimitReached();

    return reinterpret_cast<char*>(start) + offset;
  }
}

inline Arena* Segment::getArena() {
  return arena;
}
inline const Arena* Segment::getArena() const {
  return arena;
}
inline uint32_t Segment::getSegmentId() const {
  return id;
}

}  // namespace capnproto

#endif  // CAPNPROTO_ARENA_H_
