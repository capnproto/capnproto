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

// This header contains internal interfaces relied upon by message.h and implemented in message.c++.
// These declarations should be thought of as being part of message.h, but I moved them here to make
// message.h more readable.  The problem is that the interface people really care about in
// message.h -- namely, Message -- has to be declared after internal::Message.  Having an internal
// interface appear in the middle of the header ahead of the public interface was distracting and
// confusing.

#ifndef CAPNPROTO_MESSAGE_INTERNAL_H_
#define CAPNPROTO_MESSAGE_INTERNAL_H_

#include <cstddef>
#include <memory>
#include "type-safety.h"
#include "wire-format.h"

namespace capnproto {
  class ReaderContext;
  class BuilderContext;
}

namespace capnproto {
namespace internal {
// TODO:  Move to message-internal.h so that this header looks nicer?

class ReaderArena;
class BuilderArena;

struct MessageImpl {
  // Underlying implementation of capnproto::Message.  All the parts that don't need to be templated
  // are implemented by this class, so that they can be shared and non-inline.

  MessageImpl() = delete;

  class Reader {
  public:
    Reader(ArrayPtr<const ArrayPtr<const word>> segments);
    Reader(std::unique_ptr<ReaderContext> context);
    Reader(Reader&& other) = default;
    CAPNPROTO_DISALLOW_COPY(Reader);
    ~Reader();

    StructReader getRoot(const word* defaultValue);

  private:
    uint recursionLimit;

    // Space in which we can construct a ReaderArena.  We don't use ReaderArena directly here
    // because we don't want clients to have to #include arena.h, which itself includes a bunch of
    // big STL headers.  We don't use a pointer to a ReaderArena because that would require an
    // extra malloc on every message which could be expensive when processing small messages,
    // particularly when the context itself is freelisted and so no other allocation is necessary.
    void* arenaSpace[15];

    ReaderArena* arena() { return reinterpret_cast<ReaderArena*>(arenaSpace); }
  };

  class Builder {
  public:
    Builder();
    Builder(std::unique_ptr<BuilderContext> context);
    Builder(Builder&& other) = default;
    CAPNPROTO_DISALLOW_COPY(Builder);
    ~Builder();

    StructBuilder initRoot(const word* defaultValue);
    StructBuilder getRoot(const word* defaultValue);

    ArrayPtr<const ArrayPtr<const word>> getSegmentsForOutput();

  private:
    SegmentBuilder* rootSegment;

    // Space in which we can construct a BuilderArena.  We don't use BuilderArena directly here
    // because we don't want clients to have to #include arena.h, which itself includes a bunch of
    // big STL headers.  We don't use a pointer to a BuilderArena because that would require an
    // extra malloc on every message which could be expensive when processing small messages,
    // particularly when the context itself is freelisted and so no other allocation is necessary.
    void* arenaSpace[15];

    BuilderArena* arena() { return reinterpret_cast<BuilderArena*>(arenaSpace); }

    static SegmentBuilder* allocateRoot(BuilderArena* arena);
  };
};

}  // namespace internal
}  // namespace capnproto

#endif  // CAPNPROTO_MESSAGE_INTERNAL_H_
