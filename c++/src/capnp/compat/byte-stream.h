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

namespace capnp {

class ByteStreamFactory {
  // In order to allow path-shortening through KJ, a common factory must be used for converting
  // between RPC ByteStreams and KJ streams.

public:
  capnp::ByteStream::Client kjToCapnp(kj::Own<kj::AsyncOutputStream> kjStream);
  kj::Own<kj::AsyncOutputStream> capnpToKj(capnp::ByteStream::Client capnpStream);

private:
  CapabilityServerSet<capnp::ByteStream> streamSet;

  class StreamServerBase;
  class SubstreamImpl;
  class CapnpToKjStreamAdapter;
  class KjToCapnpStreamAdapter;
};

}  // namespace capnp
