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

#ifndef CAPNP_ARENA_H_
#define CAPNP_ARENA_H_

#ifndef CAPNP_PRIVATE
#error "This header is only meant to be included by Cap'n Proto's own source code."
#endif

#include <vector>
#include <unordered_map>
#include <kj/common.h>
#include <kj/mutex.h>
#include "common.h"
#include "message.h"
#include "layout.h"

namespace capnp {

class CapExtractorBase;
class CapInjectorBase;
class ClientHook;

namespace _ {  // private

class SegmentReader;
class SegmentBuilder;
class Arena;
class BasicReaderArena;
class ImbuedReaderArena;
class BuilderArena;
class BasicBuilderArena;
class ImbuedBuilderArena;
class ReadLimiter;

class Segment;
typedef kj::Id<uint32_t, Segment> SegmentId;

class ReadLimiter {
  // Used to keep track of how much data has been processed from a message, and cut off further
  // processing if and when a particular limit is reached.  This is primarily intended to guard
  // against maliciously-crafted messages which contain cycles or overlapping structures.  Cycles
  // and overlapping are not permitted by the Cap'n Proto format because in many cases they could
  // be used to craft a deceptively small message which could consume excessive server resources to
  // process, perhaps even sending it into an infinite loop.  Actually detecting overlaps would be
  // time-consuming, so instead we just keep track of how many words worth of data structures the
  // receiver has actually dereferenced and error out if this gets too high.
  //
  // This counting takes place as you call getters (for non-primitive values) on the message
  // readers.  If you call the same getter twice, the data it returns may be double-counted.  This
  // should not be a big deal in most cases -- just set the read limit high enough that it will
  // only trigger in unreasonable cases.
  //
  // This class is "safe" to use from multiple threads for its intended use case.  Threads may
  // overwrite each others' changes to the counter, but this is OK because it only means that the
  // limit is enforced a bit less strictly -- it will still kick in eventually.

public:
  inline explicit ReadLimiter();                     // No limit.
  inline explicit ReadLimiter(WordCount64 limit);    // Limit to the given number of words.

  inline void reset(WordCount64 limit);

  KJ_ALWAYS_INLINE(bool canRead(WordCount amount, Arena* arena));

  void unread(WordCount64 amount);
  // Adds back some words to the limit.  Useful when the caller knows they are double-reading
  // some data.

private:
  volatile uint64_t limit;
  // Current limit, decremented each time catRead() is called.  Volatile because multiple threads
  // could be trying to modify it at once.  (This is not real thread-safety, but good enough for
  // the purpose of this class.  See class comment.)

  KJ_DISALLOW_COPY(ReadLimiter);
};

class SegmentReader {
public:
  inline SegmentReader(Arena* arena, SegmentId id, kj::ArrayPtr<const word> ptr,
                       ReadLimiter* readLimiter);
  inline SegmentReader(Arena* arena, const SegmentReader& base);

  KJ_ALWAYS_INLINE(bool containsInterval(const void* from, const void* to));

  inline Arena* getArena();
  inline SegmentId getSegmentId();

  inline const word* getStartPtr();
  inline WordCount getOffsetTo(const word* ptr);
  inline WordCount getSize();

  inline kj::ArrayPtr<const word> getArray();

  inline void unread(WordCount64 amount);
  // Add back some words to the ReadLimiter.

private:
  Arena* arena;
  SegmentId id;
  kj::ArrayPtr<const word> ptr;
  ReadLimiter* readLimiter;

  KJ_DISALLOW_COPY(SegmentReader);

  friend class SegmentBuilder;
  friend class ImbuedSegmentBuilder;
};

class SegmentBuilder: public SegmentReader {
public:
  inline SegmentBuilder(BuilderArena* arena, SegmentId id, kj::ArrayPtr<word> ptr,
                        ReadLimiter* readLimiter, word** pos);

  KJ_ALWAYS_INLINE(word* allocate(WordCount amount));
  inline word* getPtrUnchecked(WordCount offset);

  inline BuilderArena* getArena();

  inline kj::ArrayPtr<const word> currentlyAllocated();

  inline void reset();

private:
  word** pos;
  // Pointer to a pointer to the current end point of the segment, i.e. the location where the
  // next object should be allocated.  The extra level of indirection allows an
  // ImbuedSegmentBuilder to share this pointer with the underlying BasicSegmentBuilder.

  friend class ImbuedSegmentBuilder;

  KJ_DISALLOW_COPY(SegmentBuilder);
};

class BasicSegmentBuilder: public SegmentBuilder {
public:
  inline BasicSegmentBuilder(BuilderArena* arena, SegmentId id, kj::ArrayPtr<word> ptr,
                             ReadLimiter* readLimiter);

private:
  word* actualPos;

  KJ_DISALLOW_COPY(BasicSegmentBuilder);
};

class ImbuedSegmentBuilder: public SegmentBuilder {
public:
  inline ImbuedSegmentBuilder(SegmentBuilder* base);
  inline ImbuedSegmentBuilder(decltype(nullptr));

  KJ_DISALLOW_COPY(ImbuedSegmentBuilder);
};

class Arena {
public:
  virtual ~Arena() noexcept(false);

  virtual SegmentReader* tryGetSegment(SegmentId id) = 0;
  // Gets the segment with the given ID, or return nullptr if no such segment exists.

  virtual void reportReadLimitReached() = 0;
  // Called to report that the read limit has been reached.  See ReadLimiter, below.  This invokes
  // the VALIDATE_INPUT() macro which may throw an exception; if it return normally, the caller
  // will need to continue with default values.

  virtual kj::Own<const ClientHook> extractCap(const _::StructReader& capDescriptor) = 0;
  // Given a StructReader for a capability descriptor embedded in the message, return the
  // corresponding capability.

  kj::Own<const ClientHook> extractNullCap();
  // Like extractCap() but called when the pointer was null.  This just returns a dummy capability
  // that throws exceptions on any call.
};

class BasicReaderArena final: public Arena {
public:
  BasicReaderArena(MessageReader* message);
  ~BasicReaderArena() noexcept(false);
  KJ_DISALLOW_COPY(BasicReaderArena);

  // implements Arena ------------------------------------------------
  SegmentReader* tryGetSegment(SegmentId id) override;
  void reportReadLimitReached() override;
  kj::Own<const ClientHook> extractCap(const _::StructReader& capDescriptor);

private:
  MessageReader* message;
  ReadLimiter readLimiter;

  // Optimize for single-segment messages so that small messages are handled quickly.
  SegmentReader segment0;

  typedef std::unordered_map<uint, kj::Own<SegmentReader>> SegmentMap;
  kj::MutexGuarded<kj::Maybe<kj::Own<SegmentMap>>> moreSegments;
};

class ImbuedReaderArena final: public Arena {
public:
  ImbuedReaderArena(Arena* base, CapExtractorBase* capExtractor);
  ~ImbuedReaderArena() noexcept(false);
  KJ_DISALLOW_COPY(ImbuedReaderArena);

  SegmentReader* imbue(SegmentReader* base);

  // implements Arena ------------------------------------------------
  SegmentReader* tryGetSegment(SegmentId id) override;
  void reportReadLimitReached() override;
  kj::Own<const ClientHook> extractCap(const _::StructReader& capDescriptor);

private:
  Arena* base;
  CapExtractorBase* capExtractor;

  // Optimize for single-segment messages so that small messages are handled quickly.
  SegmentReader segment0;

  typedef std::unordered_map<SegmentReader*, kj::Own<SegmentReader>> SegmentMap;
  kj::MutexGuarded<kj::Maybe<kj::Own<SegmentMap>>> moreSegments;
};

class BuilderArena: public Arena {
public:
  virtual ~BuilderArena() noexcept(false);

  virtual SegmentBuilder* getSegment(SegmentId id) = 0;
  // Get the segment with the given id.  Crashes or throws an exception if no such segment exists.

  struct AllocateResult {
    SegmentBuilder* segment;
    word* words;
  };

  virtual AllocateResult allocate(WordCount amount) = 0;
  // Find a segment with at least the given amount of space available and allocate the space.
  // Note that allocating directly from a particular segment is much faster, but allocating from
  // the arena is guaranteed to succeed.  Therefore callers should try to allocate from a specific
  // segment first if there is one, then fall back to the arena.

  virtual OrphanBuilder injectCap(kj::Own<const ClientHook>&& cap) = 0;
  // Add the capability to the message and initialize the given pointer as an interface pointer
  // pointing to this cap.

  virtual void dropCap(const StructReader& capDescriptor) = 0;
  // Remove a capability injected earlier.  Called when the pointer is overwritten or zero'd out.
};

class BasicBuilderArena final: public BuilderArena {
  // A BuilderArena that does not allow the injection of capabilities.

public:
  BasicBuilderArena(MessageBuilder* message);
  ~BasicBuilderArena() noexcept(false);
  KJ_DISALLOW_COPY(BasicBuilderArena);

  inline SegmentBuilder* getRootSegment() { return &segment0; }

  kj::ArrayPtr<const kj::ArrayPtr<const word>> getSegmentsForOutput();
  // Get an array of all the segments, suitable for writing out.  This only returns the allocated
  // portion of each segment, whereas tryGetSegment() returns something that includes
  // not-yet-allocated space.

  // implements Arena ------------------------------------------------
  SegmentReader* tryGetSegment(SegmentId id) override;
  void reportReadLimitReached() override;
  kj::Own<const ClientHook> extractCap(const _::StructReader& capDescriptor);

  // implements BuilderArena -----------------------------------------
  SegmentBuilder* getSegment(SegmentId id) override;
  AllocateResult allocate(WordCount amount) override;
  OrphanBuilder injectCap(kj::Own<const ClientHook>&& cap);
  void dropCap(const StructReader& capDescriptor);

private:
  MessageBuilder* message;
  ReadLimiter dummyLimiter;

  BasicSegmentBuilder segment0;
  kj::ArrayPtr<const word> segment0ForOutput;

  struct MultiSegmentState {
    std::vector<kj::Own<BasicSegmentBuilder>> builders;
    std::vector<kj::ArrayPtr<const word>> forOutput;
  };
  kj::MutexGuarded<kj::Maybe<kj::Own<MultiSegmentState>>> moreSegments;
};

class ImbuedBuilderArena final: public BuilderArena {
  // A BuilderArena imbued with the ability to inject capabilities.

public:
  ImbuedBuilderArena(BuilderArena* base, CapInjectorBase* capInjector);
  ~ImbuedBuilderArena() noexcept(false);
  KJ_DISALLOW_COPY(ImbuedBuilderArena);

  SegmentBuilder* imbue(SegmentBuilder* baseSegment);
  // Return an imbued SegmentBuilder corresponding to the given segment from the base arena.

  // implements Arena ------------------------------------------------
  SegmentReader* tryGetSegment(SegmentId id) override;
  void reportReadLimitReached() override;
  kj::Own<const ClientHook> extractCap(const _::StructReader& capDescriptor);

  // implements BuilderArena -----------------------------------------
  SegmentBuilder* getSegment(SegmentId id) override;
  AllocateResult allocate(WordCount amount) override;
  OrphanBuilder injectCap(kj::Own<const ClientHook>&& cap);
  void dropCap(const StructReader& capDescriptor);

private:
  BuilderArena* base;
  CapInjectorBase* capInjector;

  ImbuedSegmentBuilder segment0;

  struct MultiSegmentState {
    std::vector<kj::Maybe<kj::Own<ImbuedSegmentBuilder>>> builders;
  };
  kj::MutexGuarded<kj::Maybe<kj::Own<MultiSegmentState>>> moreSegments;
};

// =======================================================================================

inline ReadLimiter::ReadLimiter()
    // I didn't want to #include <limits> just for this one lousy constant.
    : limit(0x7fffffffffffffffllu) {}

inline ReadLimiter::ReadLimiter(WordCount64 limit): limit(limit / WORDS) {}

inline void ReadLimiter::reset(WordCount64 limit) { this->limit = limit / WORDS; }

inline bool ReadLimiter::canRead(WordCount amount, Arena* arena) {
  // Be careful not to store an underflowed value into `limit`, even if multiple threads are
  // decrementing it.
  uint64_t current = limit;
  if (KJ_UNLIKELY(amount / WORDS > current)) {
    arena->reportReadLimitReached();
    return false;
  } else {
    limit = current - amount / WORDS;
    return true;
  }
}

// -------------------------------------------------------------------

inline SegmentReader::SegmentReader(Arena* arena, SegmentId id, kj::ArrayPtr<const word> ptr,
                                    ReadLimiter* readLimiter)
    : arena(arena), id(id), ptr(ptr), readLimiter(readLimiter) {}

inline SegmentReader::SegmentReader(Arena* arena, const SegmentReader& base)
    : arena(arena), id(base.id), ptr(base.ptr), readLimiter(base.readLimiter) {}

inline bool SegmentReader::containsInterval(const void* from, const void* to) {
  return from >= this->ptr.begin() && to <= this->ptr.end() &&
      readLimiter->canRead(
          intervalLength(reinterpret_cast<const byte*>(from),
                         reinterpret_cast<const byte*>(to)) / BYTES_PER_WORD,
          arena);
}

inline Arena* SegmentReader::getArena() { return arena; }
inline SegmentId SegmentReader::getSegmentId() { return id; }
inline const word* SegmentReader::getStartPtr() { return ptr.begin(); }
inline WordCount SegmentReader::getOffsetTo(const word* ptr) {
  return intervalLength(this->ptr.begin(), ptr);
}
inline WordCount SegmentReader::getSize() { return ptr.size() * WORDS; }
inline kj::ArrayPtr<const word> SegmentReader::getArray() { return ptr; }
inline void SegmentReader::unread(WordCount64 amount) { readLimiter->unread(amount); }

// -------------------------------------------------------------------

inline SegmentBuilder::SegmentBuilder(
    BuilderArena* arena, SegmentId id, kj::ArrayPtr<word> ptr, ReadLimiter* readLimiter, word** pos)
    : SegmentReader(arena, id, ptr, readLimiter), pos(pos) {}

inline word* SegmentBuilder::allocate(WordCount amount) {
  word* result = __atomic_fetch_add(pos, amount * BYTES_PER_WORD / BYTES, __ATOMIC_RELAXED);

  // Careful about pointer arithmetic here.  The segment might be at the end of the address space,
  // or `amount` could be ridiculously huge.
  if (ptr.end() - (result + amount) < 0) {
    // Not enough space in the segment for this allocation.
    if (ptr.end() - result >= 0) {
      // It was our increment that pushed the pointer past the end of the segment.  Therefore no
      // other thread could have accidentally allocated space in this segment in the meantime.
      // We need to back up the pointer so that it will be correct when the data is written out
      // (and also so that another allocation can potentially use the remaining space).
      __atomic_store_n(pos, result, __ATOMIC_RELAXED);
    }
    return nullptr;
  } else {
    // Success.
    return result;
  }
}

inline word* SegmentBuilder::getPtrUnchecked(WordCount offset) {
  // const_cast OK because SegmentBuilder's constructor always initializes its SegmentReader base
  // class with a pointer that was originally non-const.
  return const_cast<word*>(ptr.begin() + offset);
}

inline BuilderArena* SegmentBuilder::getArena() {
  // Down-cast safe because SegmentBuilder's constructor always initializes its SegmentReader base
  // class with an Arena pointer that actually points to a BuilderArena.
  return static_cast<BuilderArena*>(arena);
}

inline kj::ArrayPtr<const word> SegmentBuilder::currentlyAllocated() {
  return kj::arrayPtr(ptr.begin(), *pos - ptr.begin());
}

inline void SegmentBuilder::reset() {
  word* start = getPtrUnchecked(0 * WORDS);
  memset(start, 0, (*pos - start) * sizeof(word));
  *pos = start;
}

inline BasicSegmentBuilder::BasicSegmentBuilder(
    BuilderArena* arena, SegmentId id, kj::ArrayPtr<word> ptr, ReadLimiter* readLimiter)
    : SegmentBuilder(arena, id, ptr, readLimiter, &actualPos),
      actualPos(ptr.begin()) {}

inline ImbuedSegmentBuilder::ImbuedSegmentBuilder(SegmentBuilder* base)
    : SegmentBuilder(static_cast<BuilderArena*>(base->arena), base->id,
                     kj::arrayPtr(const_cast<word*>(base->ptr.begin()), base->ptr.size()),
                     base->readLimiter, base->pos) {}
inline ImbuedSegmentBuilder::ImbuedSegmentBuilder(decltype(nullptr))
    : SegmentBuilder(nullptr, SegmentId(0), nullptr, nullptr, nullptr) {}

}  // namespace _ (private)
}  // namespace capnp

#endif  // CAPNP_ARENA_H_
