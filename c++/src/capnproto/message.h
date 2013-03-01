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
#include <cstddef>
#include "macros.h"

#ifndef CAPNPROTO_MESSAGE_H_
#define CAPNPROTO_MESSAGE_H_

namespace capnproto {

class SegmentReader;
class SegmentBuilder;
class MessageReader;
class MessageBuilder;
class ReadLimiter;

class MessageReader {
  // Abstract interface encapsulating a readable message.  By implementing this interface, you can
  // control how memory is allocated for the message.  Or use MallocMessage to make things easy.

public:
  virtual ~MessageReader();

  virtual SegmentReader* tryGetSegment(uint32_t index) = 0;
  // Gets the segment with the given ID, or return nullptr if no such segment exists.

  virtual void reportInvalidData(const char* description) = 0;
  // Called to report that the message data is invalid.
  //
  // Implementations should, ideally, report the error to the sender, if possible.  They may also
  // want to write a debug message, etc.
  //
  // Implementations may choose to throw an exception in order to cut short further processing of
  // the message.  If no exception is thrown, then the caller will attempt to work around the
  // invalid data by using a default value instead.  This is good enough to guard against
  // maliciously-crafted messages (the sender could just as easily have sent a perfectly-valid
  // message containing the default value), but in the case of accidentally-corrupted messages this
  // behavior may propagate the corruption.
  //
  // TODO:  Give more information about the error, e.g. the segment and offset at which the invalid
  //   data was encountered, any relevant type/field names if known, etc.

  virtual void reportReadLimitReached() = 0;
  // Called to report that the read limit has been reached.  See ReadLimiter, below.
  //
  // As with reportInvalidData(), this may throw an exception, and if it doesn't, default values
  // will be used in place of the actual message data.

  // TODO:  Methods to deal with bundled capabilities.
};

class MessageBuilder: public MessageReader {
  // Abstract interface encapsulating a writable message.  By implementing this interface, you can
  // control how memory is allocated for the message.  Or use MallocMessage to make things easy.

public:
  virtual ~MessageBuilder();

  virtual SegmentBuilder* getSegment(uint32_t id) = 0;
  // Get the segment with the given id.  Crashes or throws an exception if no such segment exists.

  virtual SegmentBuilder* getSegmentWithAvailable(uint32_t minimumSize) = 0;
  // Get a segment which has at least the given amount of space available, allocating it if
  // necessary.  Crashes or throws an exception if there is not enough memory.

  // TODO:  Methods to deal with bundled capabilities.
};

class ReadLimiter {
  // Used to keep track of how much data has been processed from a message, and cut off further
  // processing if and when a particular limit is reached.  This is primarily intended to guard
  // against maliciously-crafted messages which contain cycles or overlapping structures.  Cycles
  // and overlapping are not permitted by the Cap'n Proto format because in many cases they could
  // be used to craft a deceptively small message which could consume excessive server resources to
  // process, perhaps even sending it into an infinite loop.  Actually detecting overlaps would be
  // time-consuming, so instead we just keep track of how many bytes worth of data structures the
  // receiver has actually dereferenced and error out if this gets too high.
  //
  // This counting takes place as you call getters (for non-primitive values) on the message
  // readers.  If you call the same getter twice, the data it returns may be double-counted.  This
  // should not be a big deal in most cases -- just set the read limit high enough that it will
  // only trigger in unreasonable cases.

public:
  inline explicit ReadLimiter();                 // No limit.
  inline explicit ReadLimiter(int64_t limit);    // Limit to the given number of bytes.

  inline bool canRead(uint32_t amount);

private:
  int64_t counter;
};

class SegmentReader {
public:
  inline SegmentReader(MessageReader* message, uint32_t id, const void* ptr, uint32_t size,
                       ReadLimiter* readLimiter);

  CAPNPROTO_ALWAYS_INLINE(const void* getPtrChecked(
      uint32_t offset, uint32_t bytesBefore, uint32_t bytesAfter));

  inline MessageReader* getMessage();
  inline uint32_t getSegmentId();

  inline const void* getStartPtr();
  inline uint32_t getSize();

private:
  MessageReader* message;
  uint32_t id;
  uint32_t size;
  const void* start;
  ReadLimiter* readLimiter;

  SegmentReader(const SegmentReader& other) = delete;
  SegmentReader& operator=(const SegmentReader& other) = delete;

  void readLimitReached();

  friend class SegmentBuilder;
};

class SegmentBuilder: public SegmentReader {
public:
  inline SegmentBuilder(MessageBuilder* message, uint32_t id, void* ptr, uint32_t available);

  struct Allocation {
    void* ptr;
    uint32_t offset;

    inline Allocation(): ptr(nullptr), offset(0) {}
    inline Allocation(std::nullptr_t): ptr(nullptr), offset(0) {}
    inline Allocation(void* ptr, uint32_t offset): ptr(ptr), offset(offset) {}

    inline bool operator==(std::nullptr_t) const { return ptr == nullptr; }
  };

  CAPNPROTO_ALWAYS_INLINE(Allocation allocate(uint32_t amount));
  inline void* getPtrUnchecked(uint32_t offset);

  inline MessageBuilder* getMessage();

private:
  char* pos;
  char* end;
  ReadLimiter dummyLimiter;

  SegmentBuilder(const SegmentBuilder& other) = delete;
  SegmentBuilder& operator=(const SegmentBuilder& other) = delete;

  // TODO:  Do we need mutex locking?
};

// =======================================================================================

inline ReadLimiter::ReadLimiter()
    // I didn't want to #include <limits> just for this one lousy constant.
    : counter(0x7fffffffffffffffll) {}

inline ReadLimiter::ReadLimiter(int64_t limit): counter(limit) {}

inline bool ReadLimiter::canRead(uint32_t amount) {
  return (counter -= amount) >= 0;
}

// -------------------------------------------------------------------

inline SegmentReader::SegmentReader(
    MessageReader* message, uint32_t id, const void* ptr, uint32_t size, ReadLimiter* readLimiter)
    : message(message), id(id), size(size), start(ptr), readLimiter(readLimiter) {}

inline const void* SegmentReader::getPtrChecked(uint32_t offset, uint32_t bytesBefore,
                                                uint32_t bytesAfter) {
  // Check bounds.  Watch out for overflow and underflow here.
  if (offset > size || bytesBefore > offset || bytesAfter > size - offset) {
    return nullptr;
  } else {
    // Enforce the read limit.  Synchronization is not necessary because readLimit is just a rough
    // counter to prevent infinite loops leading to DoS attacks.
    if (CAPNPROTO_EXPECT_FALSE(!readLimiter->canRead(bytesBefore + bytesAfter))) {
      message->reportReadLimitReached();
    }

    return reinterpret_cast<const char*>(start) + offset;
  }
}

inline MessageReader* SegmentReader::getMessage() { return message; }
inline uint32_t SegmentReader::getSegmentId() { return id; }
inline const void* SegmentReader::getStartPtr() { return start; }
inline uint32_t SegmentReader::getSize() { return size; }

// -------------------------------------------------------------------

inline SegmentBuilder::SegmentBuilder(
    MessageBuilder* message, uint32_t id, void* ptr, uint32_t available)
    : SegmentReader(message, id, ptr, 0, &dummyLimiter),
      pos(reinterpret_cast<char*>(ptr)),
      end(pos + available) {}

inline SegmentBuilder::Allocation SegmentBuilder::allocate(uint32_t amount) {
  if (amount > end - pos) {
    return nullptr;
  } else {
    char* result = pos;
    pos += amount;
    size += amount;
    return Allocation(result, result - reinterpret_cast<const char*>(start));
  }
}

inline void* SegmentBuilder::getPtrUnchecked(uint32_t offset) {
  // const_cast OK because SegmentBuilder's constructor always initializes its SegmentReader base
  // class with a pointer that was originally non-const.
  return const_cast<char*>(reinterpret_cast<const char*>(start) + offset);
}

inline MessageBuilder* SegmentBuilder::getMessage() {
  // Down-cast safe because SegmentBuilder's constructor always initializes its SegmentReader base
  // class with a MessageReader pointer that actually points to a MessageBuilder.
  return static_cast<MessageBuilder*>(message);
}

}  // namespace capnproto

#endif  // CAPNPROTO_MESSAGE_H_
