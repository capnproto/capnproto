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

#ifndef CAPNP_SERIALIZE_ASYNC_H_
#define CAPNP_SERIALIZE_ASYNC_H_

#include <kj/async-io.h>
#include "message.h"

namespace capnp {

kj::Promise<kj::Own<MessageReader>> readMessage(
    kj::AsyncInputStream& input, ReaderOptions options = ReaderOptions(),
    kj::ArrayPtr<word> scratchSpace = nullptr);
// Read a message asynchronously.
//
// `input` must remain valid until the returned promise resolves (or is canceled).
//
// `scratchSpace`, if provided, must remain valid until the returned MessageReader is destroyed.

kj::Promise<kj::Maybe<kj::Own<MessageReader>>> tryReadMessage(
    kj::AsyncInputStream& input, ReaderOptions options = ReaderOptions(),
    kj::ArrayPtr<word> scratchSpace = nullptr);
// Like `readMessage` but returns null on EOF.

kj::Promise<void> writeMessage(kj::AsyncOutputStream& output,
                               kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)
    KJ_WARN_UNUSED_RESULT;
kj::Promise<void> writeMessage(kj::AsyncOutputStream& output, MessageBuilder& builder)
    KJ_WARN_UNUSED_RESULT;
// Write asynchronously.  The parameters must remain valid until the returned promise resolves.

// =======================================================================================
// inline implementation details

inline kj::Promise<void> writeMessage(kj::AsyncOutputStream& output, MessageBuilder& builder) {
  return writeMessage(output, builder.getSegmentsForOutput());
}

}  // namespace capnp

#endif  // CAPNP_SERIALIZE_ASYNC_H_
