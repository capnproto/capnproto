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

// -----------------------------------------------------------------------------
// Versions virtualized over an interface.

class MessageTransport {
  // Interface over which messages can be sent and received; virtualizes
  // the functionality above.
public:
  virtual kj::Promise<kj::Maybe<MessageReaderAndFds>> tryReadMessage(
      kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
      ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr) = 0;

  virtual kj::Promise<void> writeMessage(
      kj::ArrayPtr<const int> fds,
      kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)
    KJ_WARN_UNUSED_RESULT = 0;
  // The tryReadMessage & writeMessages methods have the same contracts as
  // the corresponding FD-oriented stand-alone functions above, except that the
  // first argument (of type AsyncCapabilityStream) is omitted.
  //
  // Below, we provide helper functions that correspond to the rest of the top-level
  // functions above, but take a MessageTransport& as their first argument.
  //
  // Implementations which do not support FD passing may simply ignore the relevant
  // arguments.

  virtual kj::Maybe<int> getSendBufferSize() = 0;
  // Get the size of the underlying send buffer, if applicable. The RPC
  // system uses this as a hint for flow control purposes; see:
  //
  // https://capnproto.org/news/#multi-stream-flow-control
  //
  // ...for a more thorough explanation of how this is used. Implementations
  // may return nullptr if they do not have access to this information, or if
  // the underlying transport does not use a congestion window.

  virtual kj::Promise<void> shutdownWrite() = 0;
  // Cleanly shut down just the write end of the transport, while keeping the read end open.

};

class AsyncIoTransport: public MessageTransport {
  // A MessageTransport that wraps an AsyncIoStream.
public:
  explicit AsyncIoTransport(kj::AsyncIoStream& stream);

  // Implements MessageTransport
  kj::Promise<kj::Maybe<MessageReaderAndFds>> tryReadMessage(
      kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
      ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr) override;
  kj::Promise<void> writeMessage(
      kj::ArrayPtr<const int> fds,
      kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) override;
  kj::Maybe<int> getSendBufferSize() override;

  kj::Promise<void> shutdownWrite() override;
private:
  kj::AsyncIoStream& stream;
};

class AsyncCapabilityTransport: public MessageTransport {
  // A MessageTransport that wraps an AsyncCapabilityStream.
public:
  explicit AsyncCapabilityTransport(kj::AsyncCapabilityStream& stream);

  // Implements MessageTransport
  kj::Promise<kj::Maybe<MessageReaderAndFds>> tryReadMessage(
      kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
      ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr) override;
  kj::Promise<void> writeMessage(
      kj::ArrayPtr<const int> fds,
      kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) override;
  kj::Maybe<int> getSendBufferSize() override;
  kj::Promise<void> shutdownWrite() override;
private:
  kj::AsyncCapabilityStream& stream;
};

kj::Promise<kj::Own<MessageReader>> readMessage(
    MessageTransport& input, ReaderOptions options = ReaderOptions(),
    kj::ArrayPtr<word> scratchSpace = nullptr);
// Read a message asynchronously.
//
// `input` must remain valid until the returned promise resolves (or is canceled).
//
// `scratchSpace`, if provided, must remain valid until the returned MessageReader is destroyed.

kj::Promise<kj::Maybe<kj::Own<MessageReader>>> tryReadMessage(
    MessageTransport& input,
    ReaderOptions options = ReaderOptions(),
    kj::ArrayPtr<word> scratchSpace = nullptr);
// Like `readMessage` but returns null on EOF.

kj::Promise<void> writeMessage(MessageTransport& output,
                               kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)
    KJ_WARN_UNUSED_RESULT;
kj::Promise<void> writeMessage(MessageTransport& output, MessageBuilder& builder)
    KJ_WARN_UNUSED_RESULT;
// Write asynchronously.  The parameters must remain valid until the returned promise resolves.

kj::Promise<MessageReaderAndFds> readMessage(
    MessageTransport& input, kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr);
// Read a message that may also have file descriptors attached, e.g. from a Unix socket with
// SCM_RIGHTS.

kj::Promise<kj::Maybe<MessageReaderAndFds>> tryReadMessage(
    MessageTransport& input, kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr);
// Like `readMessage` but returns null on EOF.

kj::Promise<void> writeMessage(MessageTransport& output, kj::ArrayPtr<const int> fds,
                               kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)
    KJ_WARN_UNUSED_RESULT;
kj::Promise<void> writeMessage(MessageTransport& output, kj::ArrayPtr<const int> fds,
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
inline kj::Promise<void> writeMessage(
    MessageTransport& output, kj::ArrayPtr<const int> fds,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments)  {
  return output.writeMessage(fds, segments);
}
inline kj::Promise<void> writeMessage(
    MessageTransport& output,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  return writeMessage(output, nullptr, segments);
}
inline kj::Promise<void> writeMessage(MessageTransport& output, MessageBuilder& builder) {
  return writeMessage(output, builder.getSegmentsForOutput());
}
inline kj::Promise<void> writeMessage(
    MessageTransport& output, kj::ArrayPtr<const int> fds, MessageBuilder& builder) {
  return writeMessage(output, fds, builder.getSegmentsForOutput());
}

}  // namespace capnp

CAPNP_END_HEADER
