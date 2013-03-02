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

#include <cstddef>
#include "macros.h"
#include "type-safety.h"

#ifndef CAPNPROTO_MESSAGE_H_
#define CAPNPROTO_MESSAGE_H_

namespace capnproto {

class SegmentReader;
class SegmentBuilder;
class MessageReader;
class MessageBuilder;
class ReadLimiter;

typedef Id<uint32_t, SegmentReader> SegmentId;

class MessageReader {
  // Abstract interface encapsulating a readable message.  By implementing this interface, you can
  // control how memory is allocated for the message.  Or use MallocMessage to make things easy.

public:
  virtual ~MessageReader();

  virtual SegmentReader* tryGetSegment(SegmentId id) = 0;
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
  //
  // If this method returns rather that throwing, many other errors are likely to be reported as
  // a side-effect of reading being blocked.  The MessageReader should ignore all further errors
  // after this call.

  // TODO:  Methods to deal with bundled capabilities.
};

class MessageBuilder: public MessageReader {
  // Abstract interface encapsulating a writable message.  By implementing this interface, you can
  // control how memory is allocated for the message.  Or use MallocMessage to make things easy.

public:
  virtual ~MessageBuilder();

  virtual SegmentBuilder* getSegment(SegmentId id) = 0;
  // Get the segment with the given id.  Crashes or throws an exception if no such segment exists.

  virtual SegmentBuilder* getSegmentWithAvailable(WordCount minimumAvailable) = 0;
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
  // time-consuming, so instead we just keep track of how many words worth of data structures the
  // receiver has actually dereferenced and error out if this gets too high.
  //
  // This counting takes place as you call getters (for non-primitive values) on the message
  // readers.  If you call the same getter twice, the data it returns may be double-counted.  This
  // should not be a big deal in most cases -- just set the read limit high enough that it will
  // only trigger in unreasonable cases.

public:
  inline explicit ReadLimiter();                     // No limit.
  inline explicit ReadLimiter(WordCount64 limit);    // Limit to the given number of words.

  CAPNPROTO_ALWAYS_INLINE(bool canRead(WordCount amount, MessageReader* message));

private:
  WordCount64 limit;

  CAPNPROTO_DISALLOW_COPY(ReadLimiter);
};

class SegmentReader {
public:
  inline SegmentReader(MessageReader* message, SegmentId id, const word ptr[], WordCount size,
                       ReadLimiter* readLimiter);

  CAPNPROTO_ALWAYS_INLINE(bool containsInterval(const word* from, const word* to));

  inline MessageReader* getMessage();
  inline SegmentId getSegmentId();

  inline const word* getStartPtr();
  inline WordCount getOffsetTo(const word* ptr);
  inline WordCount getSize();

private:
  MessageReader* message;
  SegmentId id;
  WordCount size;
  const word* start;
  ReadLimiter* readLimiter;

  CAPNPROTO_DISALLOW_COPY(SegmentReader);

  friend class SegmentBuilder;
};

class SegmentBuilder: public SegmentReader {
public:
  inline SegmentBuilder(MessageBuilder* message, SegmentId id, word ptr[], WordCount available);

  CAPNPROTO_ALWAYS_INLINE(word* allocate(WordCount amount));
  inline word* getPtrUnchecked(WordCount offset);

  inline MessageBuilder* getMessage();

private:
  word* pos;
  word* end;
  ReadLimiter dummyLimiter;

  CAPNPROTO_DISALLOW_COPY(SegmentBuilder);

  // TODO:  Do we need mutex locking?
};

// =======================================================================================

inline ReadLimiter::ReadLimiter()
    // I didn't want to #include <limits> just for this one lousy constant.
    : limit(uint64_t(0x7fffffffffffffffll) * WORDS) {}

inline ReadLimiter::ReadLimiter(WordCount64 limit): limit(limit) {}

inline bool ReadLimiter::canRead(WordCount amount, MessageReader* message) {
  if (CAPNPROTO_EXPECT_FALSE(amount > limit)) {
    message->reportReadLimitReached();
    return false;
  } else {
    limit -= amount;
    return true;
  }
}

// -------------------------------------------------------------------

inline SegmentReader::SegmentReader(MessageReader* message, SegmentId id, const word ptr[],
                                    WordCount size, ReadLimiter* readLimiter)
    : message(message), id(id), size(size), start(ptr), readLimiter(readLimiter) {}

inline bool SegmentReader::containsInterval(const word* from, const word* to) {
  return from >= this->start && to <= this->start + size &&
      readLimiter->canRead(intervalLength(from, to), message);
}

inline MessageReader* SegmentReader::getMessage() { return message; }
inline SegmentId SegmentReader::getSegmentId() { return id; }
inline const word* SegmentReader::getStartPtr() { return start; }
inline WordCount SegmentReader::getOffsetTo(const word* ptr) {
  return intervalLength(start, ptr);
}
inline WordCount SegmentReader::getSize() { return size; }

// -------------------------------------------------------------------

inline SegmentBuilder::SegmentBuilder(
    MessageBuilder* message, SegmentId id, word ptr[], WordCount available)
    : SegmentReader(message, id, ptr, 0 * WORDS, &dummyLimiter),
      pos(ptr),
      end(pos + available) {}

inline word* SegmentBuilder::allocate(WordCount amount) {
  if (amount > intervalLength(pos, end)) {
    return nullptr;
  } else {
    word* result = pos;
    pos += amount;
    size += amount;
    return result;
  }
}

inline word* SegmentBuilder::getPtrUnchecked(WordCount offset) {
  // const_cast OK because SegmentBuilder's constructor always initializes its SegmentReader base
  // class with a pointer that was originally non-const.
  return const_cast<word*>(start + offset);
}

inline MessageBuilder* SegmentBuilder::getMessage() {
  // Down-cast safe because SegmentBuilder's constructor always initializes its SegmentReader base
  // class with a MessageReader pointer that actually points to a MessageBuilder.
  return static_cast<MessageBuilder*>(message);
}

}  // namespace capnproto

#endif  // CAPNPROTO_MESSAGE_H_
