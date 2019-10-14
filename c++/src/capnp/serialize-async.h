// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include <kj/async-io.h>
#include "message.h"

CAPNP_BEGIN_HEADER

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

// -----------------------------------------------------------------------------
// Versions that support FD passing.

struct MessageReaderAndFds {
  kj::Own<MessageReader> reader;
  kj::ArrayPtr<kj::AutoCloseFd> fds;
};

kj::Promise<MessageReaderAndFds> readMessage(
    kj::AsyncCapabilityStream& input, kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr);
// Read a message that may also have file descriptors attached, e.g. from a Unix socket with
// SCM_RIGHTS.

kj::Promise<kj::Maybe<MessageReaderAndFds>> tryReadMessage(
    kj::AsyncCapabilityStream& input, kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr);
// Like `readMessage` but returns null on EOF.

kj::Promise<void> writeMessage(kj::AsyncCapabilityStream& output, kj::ArrayPtr<const int> fds,
                               kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)
    KJ_WARN_UNUSED_RESULT;
kj::Promise<void> writeMessage(kj::AsyncCapabilityStream& output, kj::ArrayPtr<const int> fds,
                               MessageBuilder& builder)
    KJ_WARN_UNUSED_RESULT;
// Write a message with FDs attached, e.g. to a Unix socket with SCM_RIGHTS.

// =======================================================================================
// inline implementation details

inline kj::Promise<void> writeMessage(kj::AsyncOutputStream& output, MessageBuilder& builder) {
  return writeMessage(output, builder.getSegmentsForOutput());
}
inline kj::Promise<void> writeMessage(
    kj::AsyncCapabilityStream& output, kj::ArrayPtr<const int> fds, MessageBuilder& builder) {
  return writeMessage(output, fds, builder.getSegmentsForOutput());
}

}  // namespace capnp

CAPNP_END_HEADER
