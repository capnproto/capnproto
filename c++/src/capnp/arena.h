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

#include <unordered_map>
#include <kj/common.h>
#include <kj/mutex.h>
#include <kj/exception.h>
#include <kj/vector.h>
#include "common.h"
#include "message.h"
#include "layout.h"
#include "capability.h"

namespace capnp {

class ClientHook;

namespace _ {  // private

class SegmentReader;
class SegmentBuilder;
class Arena;
class BuilderArena;
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

class BrokenCapFactory {
  // Callback for constructing broken caps.  We use this so that we can avoid arena.c++ having a
  // link-time dependency on capability code that lives in libcapnp-rpc.

public:
  virtual kj::Own<ClientHook> newBrokenCap(kj::StringPtr description) = 0;
};

class SegmentReader {
public:
  inline SegmentReader(Arena* arena, SegmentId id, kj::ArrayPtr<const word> ptr,
                       ReadLimiter* readLimiter);

  KJ_ALWAYS_INLINE(bool containsInterval(const void* from, const void* to));

  KJ_ALWAYS_INLINE(bool amplifiedRead(WordCount virtualAmount));
  // Indicates that the reader should pretend that `virtualAmount` additional data was read even
  // though no actual pointer was traversed. This is used e.g. when reading a struct list pointer
  // where the element sizes are zero -- the sender could set the list size arbitrarily high and
  // cause the receiver to iterate over this list even though the message itself is small, so we
  // need to defend agaisnt DoS attacks based on this.

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
};

class SegmentBuilder: public SegmentReader {
public:
  inline SegmentBuilder(BuilderArena* arena, SegmentId id, kj::ArrayPtr<word> ptr,
                        ReadLimiter* readLimiter);

  KJ_ALWAYS_INLINE(word* allocate(WordCount amount));
  inline word* getPtrUnchecked(WordCount offset);

  inline BuilderArena* getArena();

  inline kj::ArrayPtr<const word> currentlyAllocated();

  inline void reset();

private:
  word* pos;
  // Pointer to a pointer to the current end point of the segment, i.e. the location where the
  // next object should be allocated.

  KJ_DISALLOW_COPY(SegmentBuilder);
};

class Arena {
public:
  virtual ~Arena() noexcept(false);

  virtual SegmentReader* tryGetSegment(SegmentId id) = 0;
  // Gets the segment with the given ID, or return nullptr if no such segment exists.

  virtual void reportReadLimitReached() = 0;
  // Called to report that the read limit has been reached.  See ReadLimiter, below.  This invokes
  // the VALIDATE_INPUT() macro which may throw an exception; if it returns normally, the caller
  // will need to continue with default values.

  virtual kj::Maybe<kj::Own<ClientHook>> extractCap(uint index) = 0;
  // Extract the capability at the given index.  If the index is invalid, returns null.
};

class ReaderArena final: public Arena {
public:
  ReaderArena(MessageReader* message);
  ~ReaderArena() noexcept(false);
  KJ_DISALLOW_COPY(ReaderArena);

  inline void initCapTable(kj::Array<kj::Maybe<kj::Own<ClientHook>>> capTable) {
    // Imbues the arena with a capability table.  This is not passed to the constructor because the
    // table itself may be built based on some other part of the message (as is the case with the
    // RPC protocol).
    this->capTable = kj::mv(capTable);
  }

  // implements Arena ------------------------------------------------
  SegmentReader* tryGetSegment(SegmentId id) override;
  void reportReadLimitReached() override;
  kj::Maybe<kj::Own<ClientHook>> extractCap(uint index);

private:
  MessageReader* message;
  ReadLimiter readLimiter;
  kj::Array<kj::Maybe<kj::Own<ClientHook>>> capTable;

  // Optimize for single-segment messages so that small messages are handled quickly.
  SegmentReader segment0;

  typedef std::unordered_map<uint, kj::Own<SegmentReader>> SegmentMap;
  kj::MutexGuarded<kj::Maybe<kj::Own<SegmentMap>>> moreSegments;
  // We need to mutex-guard the segment map because we lazily initialize segments when they are
  // first requested, but a Reader is allowed to be used concurrently in multiple threads.  Luckily
  // this only applies to large messages.
  //
  // TODO(perf):  Thread-local thing instead?  Some kind of lockless map?  Or do sharing of data
  //   in a different way, where you have to construct a new MessageReader in each thread (but
  //   possibly backed by the same data)?
};

class BuilderArena final: public Arena {
  // A BuilderArena that does not allow the injection of capabilities.

public:
  BuilderArena(MessageBuilder* message);
  ~BuilderArena() noexcept(false);
  KJ_DISALLOW_COPY(BuilderArena);

  inline SegmentBuilder* getRootSegment() { return &segment0; }

  kj::ArrayPtr<const kj::ArrayPtr<const word>> getSegmentsForOutput();
  // Get an array of all the segments, suitable for writing out.  This only returns the allocated
  // portion of each segment, whereas tryGetSegment() returns something that includes
  // not-yet-allocated space.

  inline kj::ArrayPtr<kj::Maybe<kj::Own<ClientHook>>> getCapTable() { return capTable; }
  // Return the capability table.

  SegmentBuilder* getSegment(SegmentId id);
  // Get the segment with the given id.  Crashes or throws an exception if no such segment exists.

  struct AllocateResult {
    SegmentBuilder* segment;
    word* words;
  };

  AllocateResult allocate(WordCount amount);
  // Find a segment with at least the given amount of space available and allocate the space.
  // Note that allocating directly from a particular segment is much faster, but allocating from
  // the arena is guaranteed to succeed.  Therefore callers should try to allocate from a specific
  // segment first if there is one, then fall back to the arena.

  uint injectCap(kj::Own<ClientHook>&& cap);
  // Add the capability to the message and return its index.  If the same ClientHook is injected
  // twice, this may return the same index both times, but in this case dropCap() needs to be
  // called an equal number of times to actually remove the cap.

  void dropCap(uint index);
  // Remove a capability injected earlier.  Called when the pointer is overwritten or zero'd out.

  // implements Arena ------------------------------------------------
  SegmentReader* tryGetSegment(SegmentId id) override;
  void reportReadLimitReached() override;
  kj::Maybe<kj::Own<ClientHook>> extractCap(uint index);

private:
  MessageBuilder* message;
  ReadLimiter dummyLimiter;
  kj::Vector<kj::Maybe<kj::Own<ClientHook>>> capTable;

  SegmentBuilder segment0;
  kj::ArrayPtr<const word> segment0ForOutput;

  struct MultiSegmentState {
    kj::Vector<kj::Own<SegmentBuilder>> builders;
    kj::Vector<kj::ArrayPtr<const word>> forOutput;
  };
  kj::Maybe<kj::Own<MultiSegmentState>> moreSegments;
};

// =======================================================================================

inline ReadLimiter::ReadLimiter()
    : limit(kj::maxValue) {}

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

inline bool SegmentReader::containsInterval(const void* from, const void* to) {
  return from >= this->ptr.begin() && to <= this->ptr.end() && from <= to &&
      readLimiter->canRead(
          intervalLength(reinterpret_cast<const byte*>(from),
                         reinterpret_cast<const byte*>(to)) / BYTES_PER_WORD,
          arena);
}

inline bool SegmentReader::amplifiedRead(WordCount virtualAmount) {
  return readLimiter->canRead(virtualAmount, arena);
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
    BuilderArena* arena, SegmentId id, kj::ArrayPtr<word> ptr, ReadLimiter* readLimiter)
    : SegmentReader(arena, id, ptr, readLimiter), pos(ptr.begin()) {}

inline word* SegmentBuilder::allocate(WordCount amount) {
  if (intervalLength(pos, ptr.end()) < amount) {
    // Not enough space in the segment for this allocation.
    return nullptr;
  } else {
    // Success.
    word* result = pos;
    pos = pos + amount;
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
  return kj::arrayPtr(ptr.begin(), pos - ptr.begin());
}

inline void SegmentBuilder::reset() {
  word* start = getPtrUnchecked(0 * WORDS);
  memset(start, 0, (pos - start) * sizeof(word));
  pos = start;
}

}  // namespace _ (private)
}  // namespace capnp

#endif  // CAPNP_ARENA_H_
