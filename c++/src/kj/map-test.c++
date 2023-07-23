// Copyright (c) 2018 Kenton Varda and contributors
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

#include "map.h"
#include <kj/test.h>

namespace kj {
namespace _ {
namespace {

KJ_TEST("HashMap") {
  HashMap<String, int> map;

  map.insert(kj::str("foo"), 123);
  map.insert(kj::str("bar"), 456);

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 123);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("bar"_kj)) == 456);
  KJ_EXPECT(map.find("baz"_kj) == nullptr);

  map.upsert(kj::str("foo"), 789, [](int& old, uint newValue) {
    KJ_EXPECT(old == 123);
    KJ_EXPECT(newValue == 789);
    old = 321;
  });

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 321);

  KJ_EXPECT(
      map.findOrCreate("foo"_kj,
          []() -> HashMap<String, int>::Entry { KJ_FAIL_ASSERT("shouldn't have been called"); })
      == 321);
  KJ_EXPECT(map.findOrCreate("baz"_kj,
      [](){ return HashMap<String, int>::Entry { kj::str("baz"), 654 }; }) == 654);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("baz"_kj)) == 654);

  KJ_EXPECT(map.erase("bar"_kj));
  KJ_EXPECT(map.erase("baz"_kj));
  KJ_EXPECT(!map.erase("qux"_kj));

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 321);
  KJ_EXPECT(map.size() == 1);
  KJ_EXPECT(map.begin()->key == "foo");
  auto iter = map.begin();
  ++iter;
  KJ_EXPECT(iter == map.end());

  map.erase(*map.begin());
  KJ_EXPECT(map.size() == 0);
}

KJ_TEST("TreeMap") {
  TreeMap<String, int> map;

  map.insert(kj::str("foo"), 123);
  map.insert(kj::str("bar"), 456);

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 123);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("bar"_kj)) == 456);
  KJ_EXPECT(map.find("baz"_kj) == nullptr);

  map.upsert(kj::str("foo"), 789, [](int& old, uint newValue) {
    KJ_EXPECT(old == 123);
    KJ_EXPECT(newValue == 789);
    old = 321;
  });

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 321);

  KJ_EXPECT(
      map.findOrCreate("foo"_kj,
          []() -> TreeMap<String, int>::Entry { KJ_FAIL_ASSERT("shouldn't have been called"); })
      == 321);
  KJ_EXPECT(map.findOrCreate("baz"_kj,
      [](){ return TreeMap<String, int>::Entry { kj::str("baz"), 654 }; }) == 654);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("baz"_kj)) == 654);

  KJ_EXPECT(map.erase("bar"_kj));
  KJ_EXPECT(map.erase("baz"_kj));
  KJ_EXPECT(!map.erase("qux"_kj));

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 321);
  KJ_EXPECT(map.size() == 1);
  KJ_EXPECT(map.begin()->key == "foo");
  auto iter = map.begin();
  ++iter;
  KJ_EXPECT(iter == map.end());

  map.erase(*map.begin());
  KJ_EXPECT(map.size() == 0);
}

KJ_TEST("TreeMap range") {
  TreeMap<String, int> map;

  map.insert(kj::str("foo"), 1);
  map.insert(kj::str("bar"), 2);
  map.insert(kj::str("baz"), 3);
  map.insert(kj::str("qux"), 4);
  map.insert(kj::str("corge"), 5);

  {
    auto ordered = KJ_MAP(e, map) -> kj::StringPtr { return e.key; };
    KJ_ASSERT(ordered.size() == 5);
    KJ_EXPECT(ordered[0] == "bar");
    KJ_EXPECT(ordered[1] == "baz");
    KJ_EXPECT(ordered[2] == "corge");
    KJ_EXPECT(ordered[3] == "foo");
    KJ_EXPECT(ordered[4] == "qux");
  }

  {
    auto range = map.range("baz", "foo");
    auto iter = range.begin();
    KJ_EXPECT(iter->key == "baz");
    ++iter;
    KJ_EXPECT(iter->key == "corge");
    ++iter;
    KJ_EXPECT(iter == range.end());
  }

  map.eraseRange("baz", "foo");

  {
    auto ordered = KJ_MAP(e, map) -> kj::StringPtr { return e.key; };
    KJ_ASSERT(ordered.size() == 3);
    KJ_EXPECT(ordered[0] == "bar");
    KJ_EXPECT(ordered[1] == "foo");
    KJ_EXPECT(ordered[2] == "qux");
  }
}

}  // namespace
}  // namespace _
}  // namespace kj
