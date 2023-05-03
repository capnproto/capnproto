// Copyright (c) 2019 Cloudflare, Inc. and contributors
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
// Bridges from KJ streams to Cap'n Proto ByteStream RPC protocol.

#include <capnp/compat/byte-stream.capnp.h>
#include <kj/async-io.h>
#include <kj/compat/http.h>

CAPNP_BEGIN_HEADER

namespace capnp {

class ExplicitEndOutputStream: public kj::AsyncOutputStream {
  // HACK: KJ's AsyncOutputStream has a known serious design flaw in that EOF is signaled by
  //   destroying the stream object rather than by calling an explicit `end()` method. This causes
  //   some serious problems when signaling EOF requires doing additional I/O, such as when
  //   wrapping a capnp ByteStream where `end()` is an RPC call.
  //
  //   When it really must, the ByteStream implementation will honor the KJ convention by starting
  //   the RPC in its destructor and detach()ing the promise. But, this has lots of negative side
  //   effects, especially in the case where the stream is really meant to be aborted abruptly.
  //
  //   In lieu of an actual deep refactoring of KJ, ByteStreamFactory allows its caller to
  //   explicily specify when it is able to promise that it will make an explicit `end()` call.
  //   capnpToKjExplicitEnd() returns an ExplicitEndOutputStream, which expect to receive an
  //   `end()` call on clean EOF, and treats destruction without `end()` as an abort. This is used
  //   in particular within http-over-capnp to improve behavior somewhat.
public:
  virtual kj::Promise<void> end() = 0;
};

class ByteStreamFactory {
  // In order to allow path-shortening through KJ, a common factory must be used for converting
  // between RPC ByteStreams and KJ streams.

public:
  capnp::ByteStream::Client kjToCapnp(kj::Own<kj::AsyncOutputStream> kjStream);
  capnp::ByteStream::Client kjToCapnp(
      kj::Own<kj::AsyncOutputStream> kjStream, kj::Maybe<kj::Own<kj::TlsStarterCallback>> tlsStarter);
  kj::Own<kj::AsyncOutputStream> capnpToKj(capnp::ByteStream::Client capnpStream);

  kj::Own<ExplicitEndOutputStream> capnpToKjExplicitEnd(capnp::ByteStream::Client capnpStream);

private:
  CapabilityServerSet<capnp::ByteStream> streamSet;

  class StreamServerBase;
  class SubstreamImpl;
  class CapnpToKjStreamAdapter;
  class KjToCapnpStreamAdapter;
};

}  // namespace capnp

CAPNP_END_HEADER
