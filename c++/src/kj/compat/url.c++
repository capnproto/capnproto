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
#include <kj/encoding.h>
#include <kj/parse/char.h>
#include <kj/debug.h>
#include <stdlib.h>

namespace kj {

namespace {

constexpr auto ALPHAS = parse::charRange('a', 'z').orRange('A', 'Z');
constexpr auto DIGITS = parse::charRange('0', '9');
constexpr auto END_AUTHORITY = parse::anyOfChars("/?#");
constexpr auto END_PATH_PART = parse::anyOfChars("/?#");
constexpr auto END_QUERY_PART = parse::anyOfChars("&#");

constexpr auto SCHEME_CHARS = ALPHAS.orGroup(DIGITS).orAny("+-.");
constexpr auto NOT_SCHEME_CHARS = SCHEME_CHARS.invert();

constexpr auto HOST_CHARS = ALPHAS.orGroup(DIGITS).orAny(".-:[]");  // [] is for ipv6 literals

void toLower(String& text) {
  for (char& c: text) {
    if ('A' <= c && c <= 'Z') {
      c += 'a' - 'A';
    }
  }
}

Maybe<ArrayPtr<const char>> trySplit(StringPtr& text, char c) {
  KJ_IF_MAYBE(pos, text.findFirst(c)) {
    ArrayPtr<const char> result = text.slice(0, *pos);
    text = text.slice(*pos + 1);
    return result;
  } else {
    return nullptr;
  }
}

Maybe<ArrayPtr<const char>> trySplit(ArrayPtr<const char>& text, char c) {
  for (auto i: kj::indices(text)) {
    if (text[i] == c) {
      ArrayPtr<const char> result = text.slice(0, i);
      text = text.slice(i + 1, text.size());
      return result;
    }
  }
  return nullptr;
}

ArrayPtr<const char> split(StringPtr& text, const parse::CharGroup_& chars) {
  for (auto i: kj::indices(text)) {
    if (chars.contains(text[i])) {
      ArrayPtr<const char> result = text.slice(0, i);
      text = text.slice(i);
      return result;
    }
  }
  auto result = text.asArray();
  text = "";
  return result;
}

String percentDecode(ArrayPtr<const char> text, bool& hadErrors) {
  auto result = decodeUriComponent(text);
  if (result.hadErrors) hadErrors = true;
  return kj::mv(result);
}

}  // namespace

Url::~Url() noexcept(false) {}

Url Url::clone() const {
  return {
    kj::str(scheme),
    userInfo.map([](const UserInfo& ui) -> UserInfo {
      return {
        kj::str(ui.username),
        ui.password.map([](const String& s) { return kj::str(s); })
      };
    }),
    kj::str(host),
    KJ_MAP(part, path) { return kj::str(part); },
    hasTrailingSlash,
    KJ_MAP(param, query) -> QueryParam {
      return { kj::str(param.name), kj::str(param.value) };
    },
    fragment.map([](const String& s) { return kj::str(s); })
  };
}

Url Url::parse(StringPtr url, Context context) {
  return KJ_REQUIRE_NONNULL(tryParse(url, context), "invalid URL", url);
}

Maybe<Url> Url::tryParse(StringPtr text, Context context) {
  Url result;
  bool err = false;  // tracks percent-decoding errors

  if (context == HTTP_REQUEST) {
    if (!text.startsWith("/")) {
      return nullptr;
    }
  } else {
    KJ_IF_MAYBE(scheme, trySplit(text, ':')) {
      result.scheme = kj::str(*scheme);
    } else {
      // missing scheme
      return nullptr;
    }
    toLower(result.scheme);
    if (result.scheme.size() == 0 ||
        !ALPHAS.contains(result.scheme[0]) ||
        !SCHEME_CHARS.containsAll(result.scheme.slice(1))) {
      // bad scheme
      return nullptr;
    }

    if (!text.startsWith("//")) {
      // We require an authority (hostname) part.
      return nullptr;
    }
    text = text.slice(2);

    {
      auto authority = split(text, END_AUTHORITY);

      KJ_IF_MAYBE(userpass, trySplit(authority, '@')) {
        if (context != REMOTE_HREF) {
          // No user/pass allowed here.
          return nullptr;
        }
        KJ_IF_MAYBE(username, trySplit(*userpass, ':')) {
          result.userInfo = UserInfo {
            percentDecode(*username, err),
            percentDecode(*userpass, err)
          };
        } else {
          result.userInfo = UserInfo {
            percentDecode(*userpass, err),
            nullptr
          };
        }
      }

      result.host = percentDecode(authority, err);
      if (!HOST_CHARS.containsAll(result.host)) return nullptr;
      toLower(result.host);
    }
  }

  while (text.startsWith("/")) {
    text = text.slice(1);
    auto part = split(text, END_PATH_PART);
    if (part.size() == 2 && part[0] == '.' && part[1] == '.') {
      if (result.path.size() != 0) {
        result.path.removeLast();
      }
      result.hasTrailingSlash = true;
    } else if (part.size() == 0 || (part.size() == 1 && part[0] == '.')) {
      // Collapse consecutive slashes and "/./".
      result.hasTrailingSlash = true;
    } else {
      result.path.add(percentDecode(part, err));
      result.hasTrailingSlash = false;
    }
  }

  if (text.startsWith("?")) {
    do {
      text = text.slice(1);
      auto part = split(text, END_QUERY_PART);

      if (part.size() > 0) {
        KJ_IF_MAYBE(key, trySplit(part, '=')) {
          result.query.add(QueryParam { percentDecode(*key, err), percentDecode(part, err) });
        } else {
          result.query.add(QueryParam { percentDecode(part, err), nullptr });
        }
      }
    } while (text.startsWith("&"));
  }

  if (text.startsWith("#")) {
    if (context != REMOTE_HREF) {
      // No fragment allowed here.
      return nullptr;
    }
    result.fragment = percentDecode(text.slice(1), err);
  } else {
    // We should have consumed everything.
    KJ_ASSERT(text.size() == 0);
  }

  if (err) return nullptr;

  return kj::mv(result);
}

Url Url::parseRelative(StringPtr url) const {
  return KJ_REQUIRE_NONNULL(tryParseRelative(url), "invalid relative URL", url);
}

Maybe<Url> Url::tryParseRelative(StringPtr text) const {
  if (text.size() == 0) return clone();

  Url result;
  bool err = false;  // tracks percent-decoding errors

  // scheme
  {
    bool gotScheme = false;
    for (auto i: kj::indices(text)) {
      if (text[i] == ':') {
        // found valid scheme
        result.scheme = kj::str(text.slice(0, i));
        text = text.slice(i + 1);
        gotScheme = true;
        break;
      } else if (NOT_SCHEME_CHARS.contains(text[i])) {
        // no scheme
        break;
      }
    }
    if (!gotScheme) {
      // copy scheme
      result.scheme = kj::str(this->scheme);
    }
  }

  // authority
  bool hadNewAuthority = text.startsWith("//");
  if (hadNewAuthority) {
    text = text.slice(2);

    auto authority = split(text, END_AUTHORITY);

    KJ_IF_MAYBE(userpass, trySplit(authority, '@')) {
      KJ_IF_MAYBE(username, trySplit(*userpass, ':')) {
        result.userInfo = UserInfo {
          percentDecode(*username, err),
          percentDecode(*userpass, err)
        };
      } else {
        result.userInfo = UserInfo {
          percentDecode(*userpass, err),
          nullptr
        };
      }
    }

    result.host = percentDecode(authority, err);
  } else {
    // copy authority
    result.host = kj::str(this->host);
    result.userInfo = this->userInfo.map([](const UserInfo& userInfo) {
      return UserInfo {
        kj::str(userInfo.username),
        userInfo.password.map([](const String& password) { return kj::str(password); }),
      };
    });
  }

  // path
  bool hadNewPath = text.size() > 0 && text[0] != '?' && text[0] != '#';
  if (hadNewPath) {
    // There's a new path.

    if (text[0] == '/') {
      // New path is absolute, so don't copy the old path.
      text = text.slice(1);
      result.hasTrailingSlash = true;
    } else if (this->path.size() > 0) {
      // New path is relative, so start from the old path, dropping everything after the last
      // slash.
      auto slice = this->path.slice(0, this->path.size() - (this->hasTrailingSlash ? 0 : 1));
      result.path = KJ_MAP(part, slice) { return kj::str(part); };
      result.hasTrailingSlash = true;
    }

    for (;;) {
      auto part = split(text, END_PATH_PART);
      if (part.size() == 2 && part[0] == '.' && part[1] == '.') {
        if (path.size() != 0) {
          result.path.removeLast();
        }
        result.hasTrailingSlash = true;
      } else if (part.size() == 0 || (part.size() == 1 && part[0] == '.')) {
        // Collapse consecutive slashes and "/./".
        result.hasTrailingSlash = true;
      } else {
        result.path.add(percentDecode(part, err));
        result.hasTrailingSlash = false;
      }

      if (!text.startsWith("/")) break;
      text = text.slice(1);
    }
  } else if (!hadNewAuthority) {
    // copy path
    result.path = KJ_MAP(part, this->path) { return kj::str(part); };
    result.hasTrailingSlash = this->hasTrailingSlash;
  }

  if (text.startsWith("?")) {
    do {
      text = text.slice(1);
      auto part = split(text, END_QUERY_PART);

      if (part.size() > 0) {
        KJ_IF_MAYBE(key, trySplit(part, '=')) {
          result.query.add(QueryParam { percentDecode(*key, err), percentDecode(part, err) });
        } else {
          result.query.add(QueryParam { percentDecode(part, err), nullptr });
        }
      }
    } while (text.startsWith("&"));
  } else if (!hadNewAuthority && !hadNewPath) {
    // copy query
    result.query = KJ_MAP(param, this->query) {
      return QueryParam { kj::str(param.name), kj::str(param.value) };
    };
  }

  if (text.startsWith("#")) {
    result.fragment = percentDecode(text.slice(1), err);
  } else {
    // We should have consumed everything.
    KJ_ASSERT(text.size() == 0);
  }

  if (err) return nullptr;

  return kj::mv(result);
}

String Url::toString(Context context) const {
  Vector<char> chars(128);

  if (context != HTTP_REQUEST) {
    chars.addAll(scheme);
    chars.addAll(StringPtr("://"));

    if (context == REMOTE_HREF) {
      KJ_IF_MAYBE(user, userInfo) {
        chars.addAll(encodeUriComponent(user->username));
        KJ_IF_MAYBE(pass, user->password) {
          chars.add(':');
          chars.addAll(encodeUriComponent(*pass));
        }
        chars.add('@');
      }
    }

    // RFC3986 specifies that hosts can contain percent-encoding escapes while suggesting that
    // they should only be used for UTF-8 sequences. However, the DNS standard specifies a
    // different way to encode Unicode into domain names and doesn't permit any characters which
    // would need to be escaped. Meanwhile, encodeUriComponent() here would incorrectly try to
    // escape colons and brackets (e.g. around ipv6 literal addresses). So, instead, we throw if
    // the host is invalid.
    if (HOST_CHARS.containsAll(host)) {
      chars.addAll(host);
    } else {
      KJ_FAIL_REQUIRE("invalid hostname when stringifying URL", host) {
        chars.addAll(StringPtr("invalid-host"));
        break;
      }
    }
  }

  for (auto& pathPart: path) {
    // Protect against path injection.
    KJ_REQUIRE(pathPart != "" && pathPart != "." && pathPart != "..",
               "invalid name in URL path", *this) {
      continue;
    }
    chars.add('/');
    chars.addAll(encodeUriComponent(pathPart));
  }
  if (hasTrailingSlash || (path.size() == 0 && context == HTTP_REQUEST)) {
    chars.add('/');
  }

  bool first = true;
  for (auto& param: query) {
    chars.add(first ? '?' : '&');
    first = false;
    chars.addAll(encodeUriComponent(param.name));
    if (param.value.size() > 0) {
      chars.add('=');
      chars.addAll(encodeUriComponent(param.value));
    }
  }

  if (context == REMOTE_HREF) {
    KJ_IF_MAYBE(f, fragment) {
      chars.add('#');
      chars.addAll(encodeUriComponent(*f));
    }
  }

  chars.add('\0');
  return String(chars.releaseAsArray());
}

} // namespace kj
