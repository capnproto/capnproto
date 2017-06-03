// Copyright (c) 2017 Cloudflare, Inc. and contributors
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

#ifndef KJ_COMPAT_URL_H_
#define KJ_COMPAT_URL_H_

#include <kj/string.h>
#include <kj/vector.h>
#include <inttypes.h>

namespace kj {

struct Url {
  // Represents a URL (or, more accurately, a URI, but whatever).
  //
  // Can be parsed from a string and composed back into a string.

  String scheme;
  // E.g. "http", "https".

  struct UserInfo {
    String username;
    Maybe<String> password;
  };

  Maybe<UserInfo> userInfo;
  // Username / password.

  String host;
  // Hostname, including port if specified. We choose not to parse out the port because KJ's
  // network address parsing functions already accept addresses containing port numbers, and
  // because most web standards don't actually want to separate host and port.

  Vector<String> path;
  bool hasTrailingSlash = false;
  // Path, split on '/' characters. Note that the individual components of `path` could contain
  // '/' characters if they were percent-encoded in the original URL.
  //
  // No component of the path is allowed to be "", ".", nor ".."; if such components are present,
  // toString() will throw. Note that parse() and parseRelative() automatically resolve such
  // components.

  struct QueryParam {
    String name;
    String value;
  };
  Vector<QueryParam> query;
  // Query, e.g. from "?key=value&key2=value2". If a component of the query contains no '=' sign,
  // it will be parsed as a key with an empty value.

  Maybe<String> fragment;
  // The stuff after the '#' character (not including the '#' character itself), if present.

  // ---------------------------------------------------------------------------

  Url() = default;
  Url(Url&&) = default;
  ~Url() noexcept(false);
  Url& operator=(Url&&) = default;

#if __cplusplus < 201402L
  inline Url(String&& scheme, Maybe<UserInfo>&& userInfo, String&& host, Vector<String>&& path,
             bool hasTrailingSlash, Vector<QueryParam>&& query, Maybe<String>&& fragment)
      : scheme(kj::mv(scheme)), userInfo(kj::mv(userInfo)), host(kj::mv(host)), path(kj::mv(path)),
        hasTrailingSlash(hasTrailingSlash), query(kj::mv(query)), fragment(kj::mv(fragment)) {}
  // TODO(cleanup): This constructor is only here to support brace initialization in C++11. It
  //   should be removed once we upgrade to C++14.
#endif

  Url clone() const;

  enum Context {
    REMOTE_HREF,
    // A link to a remote resource. Requires an authority (hostname) section, hence this will
    // reject things like "mailto:" and "data:". This is the default context.

    HTTP_PROXY_REQUEST,
    // The URL to place in the first line of an HTTP proxy request. This includes scheme, host,
    // path, and query, but omits userInfo (which should be used to construct the Authorization
    // header) and fragment (which should not be transmitted).

    HTTP_REQUEST
    // The path to place in the first line of a regular HTTP request. This includes only the path
    // and query. Scheme, user, host, and fragment are omitted.

    // TODO(someday): Add context(s) that supports things like "mailto:", "data:", "blob:". These
    //   don't have an authority section.
  };

  kj::String toString(Context context = REMOTE_HREF) const;
  // Convert the URL to a string.

  static Url parse(StringPtr text, Context context = REMOTE_HREF);
  static Maybe<Url> tryParse(StringPtr text, Context context = REMOTE_HREF);
  // Parse an absolute URL.

  Url parseRelative(StringPtr relative) const;
  Maybe<Url> tryParseRelative(StringPtr relative) const;
  // Parse a relative URL string with this URL as the base.
};

} // namespace kj

#endif // KJ_COMPAT_URL_H_
