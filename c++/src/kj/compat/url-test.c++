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

#include "url.h"
#include <kj/debug.h>
#include <kj/test.h>

namespace kj {
namespace {

Url parseAndCheck(kj::StringPtr originalText, kj::StringPtr expectedRestringified = nullptr) {
  if (expectedRestringified == nullptr) expectedRestringified = originalText;
  auto url = Url::parse(originalText);
  KJ_EXPECT(kj::str(url) == expectedRestringified, url, originalText, expectedRestringified);
  return url;
}

KJ_TEST("parse / stringify URL") {
  {
    auto url = parseAndCheck("https://capnproto.org");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path == nullptr);
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  {
    auto url = parseAndCheck("https://capnproto.org:80");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org:80");
    KJ_EXPECT(url.path == nullptr);
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path == nullptr);
    KJ_EXPECT(url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar/");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar?baz=qux&corge#garply");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_ASSERT(url.query.size() == 2);
    KJ_EXPECT(url.query[0].name == "baz");
    KJ_EXPECT(url.query[0].value == "qux");
    KJ_EXPECT(url.query[1].name == "corge");
    KJ_EXPECT(url.query[1].value == nullptr);
    KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar/?baz=qux&corge=grault#garply");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(url.hasTrailingSlash);
    KJ_ASSERT(url.query.size() == 2);
    KJ_EXPECT(url.query[0].name == "baz");
    KJ_EXPECT(url.query[0].value == "qux");
    KJ_EXPECT(url.query[1].name == "corge");
    KJ_EXPECT(url.query[1].value == "grault");
    KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar?baz=qux#garply");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_ASSERT(url.query.size() == 1);
    KJ_EXPECT(url.query[0].name == "baz");
    KJ_EXPECT(url.query[0].value == "qux");
    KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar#garply");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/foo/bar/#garply");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org#garply");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path == nullptr);
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://capnproto.org/#garply");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path == nullptr);
    KJ_EXPECT(url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  }

  {
    auto url = parseAndCheck("https://foo@capnproto.org");
    KJ_EXPECT(url.scheme == "https");
    auto& user = KJ_ASSERT_NONNULL(url.userInfo);
    KJ_EXPECT(user.username == "foo");
    KJ_EXPECT(user.password == nullptr);
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path == nullptr);
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  {
    auto url = parseAndCheck("https://foo:1234@capnproto.org");
    KJ_EXPECT(url.scheme == "https");
    auto& user = KJ_ASSERT_NONNULL(url.userInfo);
    KJ_EXPECT(user.username == "foo");
    KJ_EXPECT(KJ_ASSERT_NONNULL(user.password) == "1234");
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path == nullptr);
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  {
    auto url = parseAndCheck("https://[2001:db8::1234]:80/foo");
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.userInfo == nullptr);
    KJ_EXPECT(url.host == "[2001:db8::1234]:80");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo"}));
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_EXPECT(url.query == nullptr);
    KJ_EXPECT(url.fragment == nullptr);
  }

  parseAndCheck("https://capnproto.org/foo/bar?", "https://capnproto.org/foo/bar");
  parseAndCheck("https://capnproto.org/foo/bar?#", "https://capnproto.org/foo/bar#");
  parseAndCheck("https://capnproto.org/foo/bar#");

  // Scheme and host are forced to lower-case.
  parseAndCheck("hTtP://capNprotO.org/fOo/bAr", "http://capnproto.org/fOo/bAr");
}

KJ_TEST("URL percent encoding") {
  parseAndCheck(
      "https://b%6fb:%61bcd@capnpr%6fto.org/f%6fo?b%61r=b%61z#q%75x",
      "https://bob:abcd@capnproto.org/foo?bar=baz#qux");

  parseAndCheck(
      "https://b\001b:\001bcd@capnproto.org/f\001o?b\001r=b\001z#q\001x",
      "https://b%01b:%01bcd@capnproto.org/f%01o?b%01r=b%01z#q%01x");

  parseAndCheck(
      "https://b b: bcd@capnproto.org/f o?b r=b z#q x",
      "https://b%20b:%20bcd@capnproto.org/f%20o?b%20r=b%20z#q%20x");
}

KJ_TEST("URL relative paths") {
  parseAndCheck(
      "https://capnproto.org/foo//bar",
      "https://capnproto.org/foo/bar");

  parseAndCheck(
      "https://capnproto.org/foo/./bar",
      "https://capnproto.org/foo/bar");

  parseAndCheck(
      "https://capnproto.org/foo/bar//",
      "https://capnproto.org/foo/bar/");

  parseAndCheck(
      "https://capnproto.org/foo/bar/.",
      "https://capnproto.org/foo/bar/");

  parseAndCheck(
      "https://capnproto.org/foo/baz/../bar",
      "https://capnproto.org/foo/bar");

  parseAndCheck(
      "https://capnproto.org/foo/bar/baz/..",
      "https://capnproto.org/foo/bar/");

  parseAndCheck(
      "https://capnproto.org/..",
      "https://capnproto.org/");

  parseAndCheck(
      "https://capnproto.org/foo/../..",
      "https://capnproto.org/");
}

KJ_TEST("URL for HTTP request") {
  {
    Url url = Url::parse("https://bob:1234@capnproto.org/foo/bar?baz=qux#corge");
    KJ_EXPECT(url.toString(Url::REMOTE_HREF) ==
        "https://bob:1234@capnproto.org/foo/bar?baz=qux#corge");
    KJ_EXPECT(url.toString(Url::HTTP_PROXY_REQUEST) == "https://capnproto.org/foo/bar?baz=qux");
    KJ_EXPECT(url.toString(Url::HTTP_REQUEST) == "/foo/bar?baz=qux");
  }

  {
    Url url = Url::parse("https://capnproto.org");
    KJ_EXPECT(url.toString(Url::REMOTE_HREF) == "https://capnproto.org");
    KJ_EXPECT(url.toString(Url::HTTP_PROXY_REQUEST) == "https://capnproto.org");
    KJ_EXPECT(url.toString(Url::HTTP_REQUEST) == "/");
  }

  {
    Url url = Url::parse("/foo/bar?baz=qux&corge", Url::HTTP_REQUEST);
    KJ_EXPECT(url.scheme == nullptr);
    KJ_EXPECT(url.host == nullptr);
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_ASSERT(url.query.size() == 2);
    KJ_EXPECT(url.query[0].name == "baz");
    KJ_EXPECT(url.query[0].value == "qux");
    KJ_EXPECT(url.query[1].name == "corge");
    KJ_EXPECT(url.query[1].value == nullptr);
  }

  {
    Url url = Url::parse("https://capnproto.org/foo/bar?baz=qux&corge", Url::HTTP_PROXY_REQUEST);
    KJ_EXPECT(url.scheme == "https");
    KJ_EXPECT(url.host == "capnproto.org");
    KJ_EXPECT(url.path.asPtr() == kj::ArrayPtr<const StringPtr>({"foo", "bar"}));
    KJ_EXPECT(!url.hasTrailingSlash);
    KJ_ASSERT(url.query.size() == 2);
    KJ_EXPECT(url.query[0].name == "baz");
    KJ_EXPECT(url.query[0].value == "qux");
    KJ_EXPECT(url.query[1].name == "corge");
    KJ_EXPECT(url.query[1].value == nullptr);
  }
}

KJ_TEST("parse URL failure") {
  KJ_EXPECT(Url::tryParse("ht/tps://capnproto.org") == nullptr);
  KJ_EXPECT(Url::tryParse("capnproto.org") == nullptr);
  KJ_EXPECT(Url::tryParse("https:foo") == nullptr);

  // percent-decode errors
  KJ_EXPECT(Url::tryParse("https://capnproto.org/f%nno") == nullptr);
  KJ_EXPECT(Url::tryParse("https://capnproto.org/foo?b%nnr=baz") == nullptr);

  // components not valid in context
  KJ_EXPECT(Url::tryParse("https://capnproto.org/foo", Url::HTTP_REQUEST) == nullptr);
  KJ_EXPECT(Url::tryParse("/foo#bar", Url::HTTP_REQUEST) == nullptr);
  KJ_EXPECT(Url::tryParse("https://bob:123@capnproto.org/foo", Url::HTTP_PROXY_REQUEST) == nullptr);
  KJ_EXPECT(Url::tryParse("https://capnproto.org/foo#bar", Url::HTTP_PROXY_REQUEST) == nullptr);
}

void parseAndCheckRelative(kj::StringPtr base, kj::StringPtr relative, kj::StringPtr expected) {
  auto parsed = Url::parse(base).parseRelative(relative);
  KJ_EXPECT(kj::str(parsed) == expected, parsed, expected);
}

KJ_TEST("parse relative URL") {
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "#grault",
                        "https://capnproto.org/foo/bar?baz=qux#grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "?grault",
                        "https://capnproto.org/foo/bar?grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "grault",
                        "https://capnproto.org/foo/grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "/grault",
                        "https://capnproto.org/grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "//grault",
                        "https://grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "//grault/garply",
                        "https://grault/garply");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "http:/grault",
                        "http://capnproto.org/grault");
  parseAndCheckRelative("https://capnproto.org/foo/bar?baz=qux#corge",
                        "/http:/grault",
                        "https://capnproto.org/http%3A/grault");
}

}  // namespace
}  // namespace kj
