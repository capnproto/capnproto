# Copyright (c) 2019 Cloudflare, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

@0xb665280aaff2e632;
# Cap'n Proto interface for HTTP.

using import "byte-stream.capnp".ByteStream;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("capnp");
$Cxx.allowCancellation;

interface HttpService {
  request @1 (request :HttpRequest, context :ClientRequestContext)
          -> (requestBody :ByteStream);
  # Perform an HTTP request.
  #
  # The client sends the request method/url/headers. The server responds with a `ByteStream` where
  # the client can make calls to stream up the request body. `requestBody` will be null in the case
  # that request.bodySize.fixed == 0.
  #
  # The server will send a response by invoking a method on `callback`.
  #
  # `request()` does not return until the server is completely done processing the request,
  # including sending the response. The client therefore must use promise pipelining to send the
  # request body. The client may request cancellation of the HTTP request by canceling the
  # `request()` call itself.

  startRequest @0 (request :HttpRequest, context :ClientRequestContext)
               -> (requestBody :ByteStream, context :ServerRequestContext);
  # DEPRECATED: Older form of `request()`. In this version, the server immediately returns a
  #   `ServerRequestContext` before it begins processing the request. This version was designed
  #   before `CallContext::setPipeline()` was introduced. At that time, it was impossible for the
  #   server to receive data sent to the `requestBody` stream until `startRequest()` had returned
  #   a stream capability to use, hence the ongoing call on the server side had to be represented
  #   using a separate capability. Now that we have `CallContext::setPipeline()`, the server can
  #   begin receiving the request body without returning from the top-level RPC, so we can now use
  #   `request()` instead of `startRequest()`. The new approach is more intuitive and avoids some
  #   unnecessary bookkeeping.
  #
  #   `HttpOverCapnpFactory` will continue to support both methods. Use the `peerOptimizationLevel`
  #   constructor parameter to specify which method to use, for backwards-compatibiltiy purposes.

  connect @2 (host :Text, headers :List(HttpHeader), down :ByteStream,
              context :ConnectClientRequestContext, settings :ConnectSettings)
          -> (up :ByteStream);
  # Setup an HTTP CONNECT proxy tunnel.
  #
  # The client sends the request host/headers together with a `down` ByteStream that will be used
  # for communication across the tunnel. The server will respond with the other side of that
  # ByteStream for two-way communication. The `context` includes callbacks which are used to
  # supply the client with headers.

  interface ClientRequestContext {
    # Provides callbacks for the server to send the response.

    startResponse @0 (response :HttpResponse) -> (body :ByteStream);
    # Server calls this method to send the response status and headers and to begin streaming the
    # response body. `body` will be null in the case that response.bodySize.fixed == 0, which is
    # required for HEAD responses and status codes 204, 205, and 304.

    startWebSocket @1 (headers :List(HttpHeader), upSocket :WebSocket)
                   -> (downSocket :WebSocket);
    # Server calls this method to indicate that the request is a valid WebSocket handshake and it
    # wishes to accept it as a WebSocket.
    #
    # Client -> Server WebSocket frames will be sent via method calls on `upSocket`, while
    # Server -> Client will be sent as calls to `downSocket`.
  }

  interface ConnectClientRequestContext {
    # Provides callbacks for the server to send the response.

    startConnect @0 (response :HttpResponse);
    # Server calls this method to let the client know that the CONNECT request has been
    # accepted. It also includes status code and header information.

    startError @1 (response :HttpResponse) -> (body :ByteStream);
    # Server calls this method to let the client know that the CONNECT request has been rejected.
  }

  interface ServerRequestContext {
    # DEPRECATED: Used only with startRequest(); see comments there.
    #
    # Represents execution of a particular request on the server side.
    #
    # Dropping this object before the request completes will cancel the request.
    #
    # ServerRequestContext is always a promise capability. The client must wait for it to
    # resolve using whenMoreResolved() in order to find out when the server is really done
    # processing the request. This will throw an exception if the server failed in some way that
    # could not be captured in the HTTP response. Note that it's possible for such an exception to
    # be thrown even after the response body has been completely transmitted.
  }
}

struct ConnectSettings {
  useTls @0 :Bool;
}

interface WebSocket {
  sendText @0 (text :Text) -> stream;
  sendData @1 (data :Data) -> stream;
  # Send a text or data frame.

  close @2 (code :UInt16, reason :Text);
  # Send a close frame.
}

struct HttpRequest {
  # Standard HTTP request metadata.

  method @0 :HttpMethod;
  url @1 :Text;
  headers @2 :List(HttpHeader);
  bodySize :union {
    unknown @3 :Void;   # e.g. due to transfer-encoding: chunked
    fixed @4 :UInt64;   # e.g. due to content-length
  }
}

struct HttpResponse {
  # Standard HTTP response metadata.

  statusCode @0 :UInt16;
  statusText @1 :Text;  # leave null if it matches the default for statusCode
  headers @2 :List(HttpHeader);
  bodySize :union {
    unknown @3 :Void;   # e.g. due to transfer-encoding: chunked
    fixed @4 :UInt64;   # e.g. due to content-length
  }
}

enum HttpMethod {
  # This enum aligns precisely with the kj::HttpMethod enum. However, the backwards-compat
  # constraints of a public-facing C++ enum vs. an internal Cap'n Proto interface differ in
  # several ways, which could possibly lead to divergence someday. For now, a unit test verifies
  # that they match exactly; if that test ever fails, we'll have to figure out what to do about it.

  get @0;
  head @1;
  post @2;
  put @3;
  delete @4;
  patch @5;
  purge @6;
  options @7;
  trace @8;

  copy @9;
  lock @10;
  mkcol @11;
  move @12;
  propfind @13;
  proppatch @14;
  search @15;
  unlock @16;
  acl @17;

  report @18;
  mkactivity @19;
  checkout @20;
  merge @21;

  msearch @22;
  notify @23;
  subscribe @24;
  unsubscribe @25;
}

annotation commonText @0x857745131db6fc83(enumerant) :Text;

enum CommonHeaderName {
  invalid @0;
  # Dummy to serve as default value. Should never actually appear on wire.

  acceptCharset @1 $commonText("Accept-Charset");
  acceptEncoding @2 $commonText("Accept-Encoding");
  acceptLanguage @3 $commonText("Accept-Language");
  acceptRanges @4 $commonText("Accept-Ranges");
  accept @5 $commonText("Accept");
  accessControlAllowOrigin @6 $commonText("Access-Control-Allow-Origin");
  age @7 $commonText("Age");
  allow @8 $commonText("Allow");
  authorization @9 $commonText("Authorization");
  cacheControl @10 $commonText("Cache-Control");
  contentDisposition @11 $commonText("Content-Disposition");
  contentEncoding @12 $commonText("Content-Encoding");
  contentLanguage @13 $commonText("Content-Language");
  contentLength @14 $commonText("Content-Length");
  contentLocation @15 $commonText("Content-Location");
  contentRange @16 $commonText("Content-Range");
  contentType @17 $commonText("Content-Type");
  cookie @18 $commonText("Cookie");
  date @19 $commonText("Date");
  etag @20 $commonText("ETag");
  expect @21 $commonText("Expect");
  expires @22 $commonText("Expires");
  from @23 $commonText("From");
  host @24 $commonText("Host");
  ifMatch @25 $commonText("If-Match");
  ifModifiedSince @26 $commonText("If-Modified-Since");
  ifNoneMatch @27 $commonText("If-None-Match");
  ifRange @28 $commonText("If-Range");
  ifUnmodifiedSince @29 $commonText("If-Unmodified-Since");
  lastModified @30 $commonText("Last-Modified");
  link @31 $commonText("Link");
  location @32 $commonText("Location");
  maxForwards @33 $commonText("Max-Forwards");
  proxyAuthenticate @34 $commonText("Proxy-Authenticate");
  proxyAuthorization @35 $commonText("Proxy-Authorization");
  range @36 $commonText("Range");
  referer @37 $commonText("Referer");
  refresh @38 $commonText("Refresh");
  retryAfter @39 $commonText("Retry-After");
  server @40 $commonText("Server");
  setCookie @41 $commonText("Set-Cookie");
  strictTransportSecurity @42 $commonText("Strict-Transport-Security");
  transferEncoding @43 $commonText("Transfer-Encoding");
  userAgent @44 $commonText("User-Agent");
  vary @45 $commonText("Vary");
  via @46 $commonText("Via");
  wwwAuthenticate @47 $commonText("WWW-Authenticate");
}

enum CommonHeaderValue {
  invalid @0;

  gzipDeflate @1 $commonText("gzip, deflate");

  # TODO(someday): "gzip, deflate" is the only common header value recognized by HPACK.
}

struct HttpHeader {
  union {
    common :group {
      name @0 :CommonHeaderName;
      union {
        commonValue @1 :CommonHeaderValue;
        value @2 :Text;
      }
    }
    uncommon @3 :NameValue;
  }

  struct NameValue {
    name @0 :Text;
    value @1 :Text;
  }
}
