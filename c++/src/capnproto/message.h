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
#include <memory>
#include "macros.h"
#include "type-safety.h"
#include "wire-format.h"

#ifndef CAPNPROTO_MESSAGE_H_
#define CAPNPROTO_MESSAGE_H_

namespace capnproto {

namespace internal {
  class ReaderArena;
  class BuilderArena;
}

class Segment;
typedef Id<uint32_t, Segment> SegmentId;

// =======================================================================================

class ErrorReporter {
  // Abstract interface for a class which receives notification of errors found in an input message.

public:
  virtual ~ErrorReporter();

  virtual void reportError(const char* description) = 0;
  // Reports an error discovered while validating a message.  This happens lazily, as the message
  // is traversed.  E.g., it can happen when a get() accessor is called for a sub-struct or list,
  // and that object is found to be out-of-bounds or has the wrong type.
  //
  // This method can throw an exception.  If it does not, then the getter that was called will
  // return the default value.  Returning a default value is sufficient to prevent invalid messages
  // from being a security threat, since an attacker could always construct a valid message
  // containing the default value to get the same effect.  However, returning a default value is
  // not ideal when handling messages that were accidentally corrupted -- it may lead to the wrong
  // behavior, e.g. storing the wrong data to disk, which could cause further problems down the
  // road.  Therefore, throwing an exception is preferred -- if your code is exception-safe, of
  // course.
};

ErrorReporter* getThrowingErrorReporter();
// Returns a singleton ErrorReporter which throws an exception (deriving from std::exception) on
// error.

ErrorReporter* getStderrErrorReporter();
// Returns a singleton ErrorReporter which prints a message to stderr on error, then replaces the
// invalid data with the default value.

ErrorReporter* getIgnoringErrorReporter();
// Returns a singleton ErrorReporter which silently replaces invalid data with its default value.

struct ReaderOptions {
  // Options controlling how data is read.

  uint64_t traversalLimitInWords = 8 * 1024 * 1024;
  // Limits how many total words of data are allowed to be traversed.  Traversal is counted when
  // a new struct or list builder is obtained, e.g. from a get() accessor.  This means that calling
  // the getter for the same sub-struct multiple times will cause it to be double-counted.  Once
  // the traversal limit is reached, an error will be reported.
  //
  // This limit exists for security reasons.  It is possible for an attacker to construct a message
  // in which multiple pointers point at the same location.  This is technically invalid, but hard
  // to detect.  Using such a message, an attacker could cause a message which is small on the wire
  // to appear much larger when actually traversed, possibly exhausting server resources leading to
  // denial-of-service.
  //
  // It makes sense to set a traversal limit that is much larger than the underlying message.
  // Together with sensible coding practices (e.g. trying to avoid calling sub-object getters
  // multiple times, which is expensive anyway), this should provide adequate protection without
  // inconvenience.
  //
  // The default limit is 64 MiB.  This may or may not be a sensible number for any given use case,
  // but probably at least prevents easy exploitation while also avoiding causing problems in most
  // typical cases.

  uint nestingLimit = 64;
  // Limits how deeply-nested a message structure can be, e.g. structs containing other structs or
  // lists of structs.
  //
  // Like the traversal limit, this limit exists for security reasons.  Since it is common to use
  // recursive code to traverse recursive data structures, an attacker could easily cause a stack
  // overflow by sending a very-deeply-nested (or even cyclic) message, without the message even
  // being very large.  The default limit of 64 is probably low enough to prevent any chance of
  // stack overflow, yet high enough that it is never a problem in practice.

  ErrorReporter* errorReporter = getThrowingErrorReporter();
  // How to report errors.
};

class MessageReader {
public:
  MessageReader(ReaderOptions options);
  // It is suggested that subclasses take ReaderOptions as a constructor parameter, but give it a
  // default value of "ReaderOptions()".  The base class constructor doesn't have a default value
  // in order to remind subclasses that they really need to give the user a way to provide this.

  virtual ~MessageReader();

  virtual ArrayPtr<const word> getSegment(uint id) = 0;
  // Gets the segment with the given ID, or returns null if no such segment exists.
  //
  // Normally getSegment() will only be called once for each segment ID.  Subclasses can call
  // reset() to clear the segment table and start over with new segments.

  inline const ReaderOptions& getOptions();
  // Get the options passed to the constructor.

  template <typename RootType>
  typename RootType::Reader getRoot();

private:
  ReaderOptions options;

  // Space in which we can construct a ReaderArena.  We don't use ReaderArena directly here
  // because we don't want clients to have to #include arena.h, which itself includes a bunch of
  // big STL headers.  We don't use a pointer to a ReaderArena because that would require an
  // extra malloc on every message which could be expensive when processing small messages.
  void* arenaSpace[15];
  bool allocatedArena;

  internal::ReaderArena* arena() { return reinterpret_cast<internal::ReaderArena*>(arenaSpace); }
  internal::StructReader getRoot(const word* defaultValue);
};

class MessageBuilder {
public:
  MessageBuilder();
  virtual ~MessageBuilder();

  virtual ArrayPtr<word> allocateSegment(uint minimumSize) = 0;
  // Allocates an array of at least the given number of words, throwing an exception or crashing if
  // this is not possible.  It is expected that this method will usually return more space than
  // requested, and the caller should use that extra space as much as possible before allocating
  // more.  The returned space remains valid at least until the MessageBuilder is destroyed.

  template <typename RootType>
  typename RootType::Builder initRoot();
  template <typename RootType>
  typename RootType::Builder getRoot();

  ArrayPtr<const ArrayPtr<const word>> getSegmentsForOutput();

private:
  // Space in which we can construct a BuilderArena.  We don't use BuilderArena directly here
  // because we don't want clients to have to #include arena.h, which itself includes a bunch of
  // big STL headers.  We don't use a pointer to a BuilderArena because that would require an
  // extra malloc on every message which could be expensive when processing small messages.
  void* arenaSpace[15];
  bool allocatedArena = false;

  internal::BuilderArena* arena() { return reinterpret_cast<internal::BuilderArena*>(arenaSpace); }
  internal::SegmentBuilder* getRootSegment();
  internal::StructBuilder initRoot(const word* defaultValue);
  internal::StructBuilder getRoot(const word* defaultValue);
};

template <typename RootType>
static typename RootType::Reader readMessageTrusted(const word* data);
// IF THE INPUT IS INVALID, THIS MAY CRASH, CORRUPT MEMORY, CREATE A SECURITY HOLE IN YOUR APP,
// MURDER YOUR FIRST-BORN CHILD, AND/OR BRING ABOUT ETERNAL DAMNATION ON ALL OF HUMANITY.  DO NOT
// USE UNLESS YOU UNDERSTAND THE CONSEQUENCES.
//
// Given a pointer to a known-valid message located in a single contiguous memory segment,
// returns a reader for that message.  No bounds-checking will be done while tranversing this
// message.  Use this only if you are absolutely sure that the input data is a valid message
// created by your own system.  Never use this to read messages received from others.
//
// To create a trusted message, build a message using a MallocAllocator whose preferred segment
// size is larger than the message size.  This guarantees that the message will be allocated as a
// single segment, meaning getSegmentsForOutput() returns a single word array.  That word array
// is your message; you may pass a pointer to its first word into readTrusted() to read the
// message.
//
// This can be particularly handy for embedding messages in generated code:  you can
// embed the raw bytes (using AlignedData) then make a Reader for it using this.  This is the way
// default values are embedded in code generated by the Cap'n Proto compiler.  E.g., if you have
// a message MyMessage, you can read its default value like so:
//    MyMessage::Reader reader = Message<MyMessage>::ReadTrusted(MyMessage::DEFAULT.words);

// =======================================================================================

class SegmentArrayMessageReader: public MessageReader {
  // A simple MessageReader that reads from an array of word arrays representing all segments.
  // In particular you can read directly from the output of MessageBuilder::getSegmentsForOutput()
  // (although it would probably make more sense to call builder.getRoot().asReader() in that case).

public:
  SegmentArrayMessageReader(ArrayPtr<const ArrayPtr<const word>> segments,
                            ReaderOptions options = ReaderOptions());
  // Creates a message pointing at the given segment array, without taking ownership of the
  // segments.  All arrays passed in must remain valid until the MessageReader is destroyed.

  CAPNPROTO_DISALLOW_COPY(SegmentArrayMessageReader);
  ~SegmentArrayMessageReader();

  virtual ArrayPtr<const word> getSegment(uint id) override;

private:
  ArrayPtr<const ArrayPtr<const word>> segments;
};

enum class AllocationStrategy: uint8_t {
  FIXED_SIZE,
  // The builder will prefer to allocate the same amount of space for each segment with no
  // heuristic growth.  It will still allocate larger segments when the preferred size is too small
  // for some single object.  This mode is generally not recommended, but can be particularly useful
  // for testing in order to force a message to allocate a predictable number of segments.  Note
  // that you can force every single object in the message to be located in a separate segment by
  // using this mode with firstSegmentWords = 0.

  GROW_HEURISTICALLY
  // The builder will heuristically decide how much space to allocate for each segment.  Each
  // allocated segment will be progressively larger than the previous segments on the assumption
  // that message sizes are exponentially distributed.  The total number of segments that will be
  // allocated for a message of size n is O(log n).
};

constexpr uint SUGGESTED_FIRST_SEGMENT_WORDS = 1024;
constexpr AllocationStrategy SUGGESTED_ALLOCATION_STRATEGY = AllocationStrategy::GROW_HEURISTICALLY;

class MallocMessageBuilder: public MessageBuilder {
  // A simple MessageBuilder that uses malloc() (actually, calloc()) to allocate segments.  This
  // implementation should be reasonable for any case that doesn't require writing the message to
  // a specific location in memory.

public:
  explicit MallocMessageBuilder(uint firstSegmentWords = 1024,
      AllocationStrategy allocationStrategy = SUGGESTED_ALLOCATION_STRATEGY);
  // Creates a BuilderContext which allocates at least the given number of words for the first
  // segment, and then uses the given strategy to decide how much to allocate for subsequent
  // segments.  When choosing a value for firstSegmentWords, consider that:
  // 1) Reading and writing messages gets slower when multiple segments are involved, so it's good
  //    if most messages fit in a single segment.
  // 2) Unused bytes will not be written to the wire, so generally it is not a big deal to allocate
  //    more space than you need.  It only becomes problematic if you are allocating many messages
  //    in parallel and thus use lots of memory, or if you allocate so much extra space that just
  //    zeroing it out becomes a bottleneck.
  // The defaults have been chosen to be reasonable for most people, so don't change them unless you
  // have reason to believe you need to.

  explicit MallocMessageBuilder(ArrayPtr<word> firstSegment,
      AllocationStrategy allocationStrategy = SUGGESTED_ALLOCATION_STRATEGY);
  // This version always returns the given array for the first segment, and then proceeds with the
  // allocation strategy.  This is useful for optimization when building lots of small messages in
  // a tight loop:  you can reuse the space for the first segment.

  CAPNPROTO_DISALLOW_COPY(MallocMessageBuilder);
  virtual ~MallocMessageBuilder();

  virtual ArrayPtr<word> allocateSegment(uint minimumSize) override;

private:
  uint nextSize;
  AllocationStrategy allocationStrategy;

  bool ownFirstSegment;
  void* firstSegment;

  struct MoreSegments;
  std::unique_ptr<MoreSegments> moreSegments;
};

// =======================================================================================
// implementation details

inline const ReaderOptions& MessageReader::getOptions() {
  return options;
}

template <typename RootType>
inline typename RootType::Reader MessageReader::getRoot() {
  return typename RootType::Reader(getRoot(RootType::DEFAULT.words));
}

template <typename RootType>
inline typename RootType::Builder MessageBuilder::initRoot() {
  return typename RootType::Builder(initRoot(RootType::DEFAULT.words));
}

template <typename RootType>
inline typename RootType::Builder MessageBuilder::getRoot() {
  return typename RootType::Builder(getRoot(RootType::DEFAULT.words));
}

template <typename RootType>
typename RootType::Reader readMessageTrusted(const word* data) {
  return typename RootType::Reader(internal::StructReader::readRootTrusted(
      data, RootType::DEFAULT.words));
}

}  // namespace capnproto

#endif  // CAPNPROTO_MESSAGE_H_
