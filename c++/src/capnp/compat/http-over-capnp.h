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
// Bridges from KJ HTTP to Cap'n Proto HTTP-over-RPC.

#include <capnp/compat/http-over-capnp.capnp.h>
#include <kj/compat/http.h>
#include <kj/map.h>
#include "byte-stream.h"

namespace capnp {

class HttpOverCapnpFactory {
public:
  HttpOverCapnpFactory(ByteStreamFactory& streamFactory,
                       kj::HttpHeaderTable::Builder& headerTableBuilder);

  kj::Own<kj::HttpService> capnpToKj(capnp::HttpService::Client rpcService);
  capnp::HttpService::Client kjToCapnp(kj::Own<kj::HttpService> service);

private:
  ByteStreamFactory& streamFactory;
  kj::HttpHeaderTable& headerTable;
  kj::Array<capnp::CommonHeaderName> nameKjToCapnp;
  kj::Array<kj::HttpHeaderId> nameCapnpToKj;
  kj::Array<kj::StringPtr> valueCapnpToKj;
  kj::HashMap<kj::StringPtr, capnp::CommonHeaderValue> valueKjToCapnp;

  class RequestState;

  class CapnpToKjWebSocketAdapter;
  class KjToCapnpWebSocketAdapter;

  class ClientRequestContextImpl;
  class KjToCapnpHttpServiceAdapter;

  class ServerRequestContextImpl;
  class CapnpToKjHttpServiceAdapter;

  kj::HttpHeaders headersToKj(capnp::List<capnp::HttpHeader>::Reader capnpHeaders) const;
  // Returned headers may alias into `capnpHeaders`.

  capnp::Orphan<capnp::List<capnp::HttpHeader>> headersToCapnp(
      const kj::HttpHeaders& headers, capnp::Orphanage orphanage);
};

}  // namespace capnp
