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
#include "message-internal.h"

#ifndef CAPNPROTO_MESSAGE_H_
#define CAPNPROTO_MESSAGE_H_

namespace capnproto {

class Segment;
typedef Id<uint32_t, Segment> SegmentId;

// =======================================================================================

class ReaderContext {
public:
  virtual ~ReaderContext();

  virtual ArrayPtr<const word> getSegment(uint id) = 0;
  // Gets the segment with the given ID, or returns null if no such segment exists.

  virtual uint64_t getReadLimit() = 0;
  virtual uint getNestingLimit() = 0;

  virtual void reportError(const char* description) = 0;
};

enum class ErrorBehavior {
  THROW_EXCEPTION,
  REPORT_TO_STDERR_AND_RETURN_DEFAULT,
  IGNORE_AND_RETURN_DEFAULT
};

std::unique_ptr<ReaderContext> newReaderContext(
    ArrayPtr<const ArrayPtr<const word>> segments,
    ErrorBehavior errorBehavior = ErrorBehavior::THROW_EXCEPTION,
    uint64_t readLimit = 64 * 1024 * 1024, uint nestingLimit = 64);
// Creates a ReaderContext pointing at the given segment list, without taking ownership of the
// segments.  All arrays passed in must remain valid until the context is destroyed.

class BuilderContext {
public:
  virtual ~BuilderContext();

  virtual ArrayPtr<word> allocateSegment(uint minimumSize) = 0;
  // Allocates an array of at least the given number of words, throwing an exception or crashing if
  // this is not possible.  It is expected that this method will usually return more space than
  // requested, and the caller should use that extra space as much as possible before allocating
  // more.  All returned space is deleted when the context is destroyed.
};

std::unique_ptr<BuilderContext> newBuilderContext(uint firstSegmentWords = 1024);
// Creates a BuilderContext which allocates at least the given number of words for the first
// segment, and then heuristically decides how much to allocate for subsequent segments.  This
// should work well for most use cases that do not require writing messages to specific locations
// in memory.  When choosing a value for firstSegmentWords, consider that:
// 1) Reading and writing messages gets slower when multiple segments are involved, so it's good
//    if most messages fit in a single segment.
// 2) Unused bytes will not be written to the wire, so generally it is not a big deal to allocate
//    more space than you need.  It only becomes problematic if you are allocating many messages
//    in parallel and thus use lots of memory, or if you allocate so much extra space that just
//    zeroing it out becomes a bottleneck.
// The default has been chosen to be reasonable for most people, so don't change it unless you have
// reason to believe you need to.

std::unique_ptr<BuilderContext> newFixedWidthBuilderContext(uint preferredSegmentWords = 1024);
// Creates a BuilderContext which will always prefer to allocate segments with the given size with
// no heuristic growth.  It will still allocate larger segments when the preferred size is too small
// for some single object.  You can force every single object to be located in a separate segment by
// passing zero for the parameter to this function, but this isn't a good idea.  This context
// implementation is probably most useful for testing purposes, where you want to verify that your
// serializer works when a message is split across segments and you want those segments to be
// somewhat predictable.

// =======================================================================================

template <typename RootType>
struct Message {
  Message() = delete;

  class Reader {
  public:
    Reader(ArrayPtr<const ArrayPtr<const word>> segments);
    // Make a Reader that reads from the given segments, as if the context were created using
    // newReaderContext(segments).

    Reader(std::unique_ptr<ReaderContext> context);

    CAPNPROTO_DISALLOW_COPY(Reader);
    Reader(Reader&& other) = default;

    typename RootType::Reader getRoot();
    // Get a reader pointing to the message root.

  private:
    internal::MessageImpl::Reader internal;
  };

  class Builder {
  public:
    Builder();
    // Make a Builder as if with a context created by newBuilderContext().

    Builder(std::unique_ptr<BuilderContext> context);

    CAPNPROTO_DISALLOW_COPY(Builder);
    Builder(Builder&& other) = default;

    typename RootType::Builder initRoot();
    // Allocate and initialize the message root.  If already initialized, the old data is discarded.

    typename RootType::Builder getRoot();
    // Get the message root, initializing it to the type's default value if it isn't initialized
    // already.

    ArrayPtr<const ArrayPtr<const word>> getSegmentsForOutput();

  private:
    internal::MessageImpl::Builder internal;
  };

  static typename RootType::Reader readTrusted(const word* data);
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
};

// =======================================================================================
// implementation details

template <typename RootType>
inline Message<RootType>::Reader::Reader(ArrayPtr<const ArrayPtr<const word>> segments)
    : internal(segments) {}

template <typename RootType>
inline Message<RootType>::Reader::Reader(std::unique_ptr<ReaderContext> context)
    : internal(std::move(context)) {}

template <typename RootType>
inline typename RootType::Reader Message<RootType>::Reader::getRoot() {
  return typename RootType::Reader(internal.getRoot(RootType::DEFAULT.words));
}

template <typename RootType>
inline Message<RootType>::Builder::Builder()
    : internal() {}

template <typename RootType>
inline Message<RootType>::Builder::Builder(std::unique_ptr<BuilderContext> context)
    : internal(std::move(context)) {}

template <typename RootType>
inline typename RootType::Builder Message<RootType>::Builder::initRoot() {
  return typename RootType::Builder(internal.initRoot(RootType::DEFAULT.words));
}

template <typename RootType>
inline typename RootType::Builder Message<RootType>::Builder::getRoot() {
  return typename RootType::Builder(internal.getRoot(RootType::DEFAULT.words));
}

template <typename RootType>
inline ArrayPtr<const ArrayPtr<const word>> Message<RootType>::Builder::getSegmentsForOutput() {
  return internal.getSegmentsForOutput();
}

template <typename RootType>
typename RootType::Reader Message<RootType>::readTrusted(const word* data) {
  return typename RootType::Reader(internal::StructReader::readRootTrusted(
      data, RootType::DEFAULT.words));
}

}  // namespace capnproto

#endif  // CAPNPROTO_MESSAGE_H_
