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

CAPNP_BEGIN_HEADER

namespace capnp {

class HttpOverCapnpFactory {
public:
  class HeaderIdBundle {
  public:
    HeaderIdBundle(kj::HttpHeaderTable::Builder& builder);

    HeaderIdBundle clone() const;

  private:
    HeaderIdBundle(const kj::HttpHeaderTable& table, kj::Array<kj::HttpHeaderId> nameCapnpToKj,
        size_t maxHeaderId);
    // Constructor for clone().

    const kj::HttpHeaderTable& table;

    kj::Array<kj::HttpHeaderId> nameCapnpToKj;
    size_t maxHeaderId = 0;

    friend class HttpOverCapnpFactory;
  };

  enum OptimizationLevel {
    // Specifies the protocol optimization level supported by the remote peer. Setting this higher
    // will improve efficiency but breaks compatibility with older peers that don't implement newer
    // levels.

    LEVEL_1,
    // Use startRequest(), the original version of the protocol.

    LEVEL_2
    // Use request(). This is more efficient than startRequest() but won't work with old peers that
    // only implement startRequest().
  };

  HttpOverCapnpFactory(ByteStreamFactory& streamFactory, HeaderIdBundle headerIds,
                       OptimizationLevel peerOptimizationLevel = LEVEL_1);

  kj::Own<kj::HttpService> capnpToKj(capnp::HttpService::Client rpcService);
  capnp::HttpService::Client kjToCapnp(kj::Own<kj::HttpService> service);

private:
  ByteStreamFactory& streamFactory;
  const kj::HttpHeaderTable& headerTable;
  OptimizationLevel peerOptimizationLevel;
  kj::Array<capnp::CommonHeaderName> nameKjToCapnp;
  kj::Array<kj::HttpHeaderId> nameCapnpToKj;
  kj::Array<kj::StringPtr> valueCapnpToKj;
  kj::HashMap<kj::StringPtr, capnp::CommonHeaderValue> valueKjToCapnp;

  class RequestState;

  class CapnpToKjWebSocketAdapter;
  class KjToCapnpWebSocketAdapter;

  class ClientRequestContextImpl;
  class ConnectClientRequestContextImpl;
  class KjToCapnpHttpServiceAdapter;

  class HttpServiceResponseImpl;
  class HttpOverCapnpConnectResponseImpl;
  class ServerRequestContextImpl;
  class CapnpToKjHttpServiceAdapter;

  kj::HttpHeaders headersToKj(capnp::List<capnp::HttpHeader>::Reader capnpHeaders) const;
  // Returned headers may alias into `capnpHeaders`.

  capnp::Orphan<capnp::List<capnp::HttpHeader>> headersToCapnp(
      const kj::HttpHeaders& headers, capnp::Orphanage orphanage);
};

}  // namespace capnp

CAPNP_END_HEADER
