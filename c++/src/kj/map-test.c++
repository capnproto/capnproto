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

  kj::String ownFoo = kj::str("foo");
  const char* origFoo = ownFoo.begin();
  map.insert(kj::mv(ownFoo), 123);
  map.insert(kj::str("bar"), 456);

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 123);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("bar"_kj)) == 456);
  KJ_EXPECT(map.find("baz"_kj) == kj::none);

  map.upsert(kj::str("foo"), 789, [](int& old, uint newValue) {
    KJ_EXPECT(old == 123);
    KJ_EXPECT(newValue == 789);
    old = 4321;
  });

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 4321);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.findEntry("foo"_kj)).key.begin() == origFoo);

  map.upsert(kj::str("foo"), 321);

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 321);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.findEntry("foo"_kj)).key.begin() == origFoo);

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

  kj::String ownFoo = kj::str("foo");
  const char* origFoo = ownFoo.begin();
  map.insert(kj::mv(ownFoo), 123);
  map.insert(kj::str("bar"), 456);

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 123);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("bar"_kj)) == 456);
  KJ_EXPECT(map.find("baz"_kj) == kj::none);

  map.upsert(kj::str("foo"), 789, [](int& old, uint newValue) {
    KJ_EXPECT(old == 123);
    KJ_EXPECT(newValue == 789);
    old = 4321;
  });

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 4321);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.findEntry("foo"_kj)).key.begin() == origFoo);

  map.upsert(kj::str("foo"), 321);

  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find("foo"_kj)) == 321);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.findEntry("foo"_kj)).key.begin() == origFoo);

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

KJ_TEST("HashMap callback API") {
  HashMap<String, int> map;
  map.insert(kj::str("foo"), 123);

  auto found = map.find("foo"_kj, [&](int& value) {
    KJ_EXPECT(value == 123);
    value = 456;
    return value * 2;
  });
  KJ_EXPECT(found.assertSome() == 912);

  auto copied = map.find("foo"_kj, [](int& value) -> int& { return value; });
  static_assert(kj::isSameType<decltype(copied), kj::Maybe<int>>());
  map.find("foo"_kj, [](int& value) { return value = 457; });
  KJ_EXPECT(copied.assertSome() == 456);

  bool called = false;
  auto missing = map.find("missing"_kj, [&](int&) {
    called = true;
  });
  static_assert(kj::isSameType<decltype(missing), kj::Maybe<void>>());
  missing.assertNone();
  KJ_EXPECT(!called);

  auto modified = map.find("foo"_kj, [](int& value) { value = 458; });
  static_assert(kj::isSameType<decltype(modified), kj::Maybe<void>>());
  modified.assertSome();

  const auto& constMap = map;
  KJ_EXPECT(constMap.find("foo"_kj, [&](const int& value) {
    return value;
  }).assertSome() == 458);

  KJ_EXPECT(map.findEntry("foo"_kj, [&](HashMap<String, int>::Entry& entry) {
    entry.value = 789;
    return entry.key.asPtr();
  }).assertSome() == "foo");
  KJ_EXPECT(constMap.findEntry("foo"_kj,
      [&](const HashMap<String, int>::Entry& entry) {
    return entry.value;
  }).assertSome() == 789);

  bool created = false;
  auto existing = map.findOrCreate("foo"_kj, [&]() -> HashMap<String, int>::Entry {
    KJ_FAIL_ASSERT("shouldn't have been called");
  }, [&](int& value) {
    KJ_EXPECT(value == 789);
    return value * 2;
  });
  KJ_EXPECT(existing == 1578);
  map.findOrCreate("bar"_kj, [&]() -> HashMap<String, int>::Entry {
    created = true;
    return { kj::str("bar"), 321 };
  }, [&](int& value) {
    KJ_EXPECT(value == 321);
    value = 654;
  });
  KJ_EXPECT(created);
  KJ_EXPECT(map.find("bar"_kj).assertSome() == 654);

  auto insertedEntry = map.findOrCreateEntry(
      "baz"_kj, [&]() -> HashMap<String, int>::Entry {
    return { kj::str("baz"), 987 };
  }, [&](HashMap<String, int>::Entry& entry) {
    KJ_EXPECT(entry.value == 987);
    return entry.key.asPtr();
  });
  KJ_EXPECT(insertedEntry == "baz");

  HashMap<String, int> upsertMap;
  auto& oldApiResult = upsertMap.upsert(kj::str("foo"), 123,
      [](int&, int&&) { KJ_FAIL_ASSERT("shouldn't have been called"); });
  KJ_EXPECT(oldApiResult.value == 123);

  auto upsertInserted = upsertMap.upsert(kj::str("bar"), 456,
      [](int&, int&&) { KJ_FAIL_ASSERT("shouldn't have been called"); },
      [](int& value) -> int& { return value; });
  static_assert(kj::isSameType<decltype(upsertInserted), int>());
  KJ_EXPECT(upsertInserted == 456);

  auto upsertUpdated = upsertMap.upsert(kj::str("foo"), 456,
      [](int& oldValue, int&& newValue) { oldValue += newValue; },
      [](int& value) { return value; });
  KJ_EXPECT(upsertUpdated == 579);

  upsertMap.upsert(kj::str("foo"), 1,
      [](int& oldValue, int&& newValue) { oldValue += newValue; },
      [](int&) {});
  KJ_EXPECT(upsertMap.find("foo"_kj).assertSome() == 580);
}

KJ_TEST("HashMap findOrCreate throws") {
  HashMap<int, String> m;
  try {
    m.findOrCreate(1, []() -> HashMap<int, String>::Entry {
      throw "foo";
    });
    KJ_FAIL_ASSERT("shouldn't get here");
  } catch (const char*) {
    // expected
  }

  KJ_EXPECT(m.find(1) == kj::none);
  m.findOrCreate(1, []() {
    return HashMap<int, String>::Entry { 1, kj::str("ok") };
  });

  KJ_EXPECT(KJ_ASSERT_NONNULL(m.find(1)) == "ok");
}

template <typename MapType>
void testEraseAll(MapType& m) {
  m.insert(12, "foo");
  m.insert(83, "bar");
  m.insert(99, "baz");
  m.insert(6, "qux");
  m.insert(55, "corge");

  auto count = m.eraseAll([](int i, StringPtr s) {
    return i == 99 || s == "foo";
  });

  KJ_EXPECT(count == 2);
  KJ_EXPECT(m.size() == 3);
  KJ_EXPECT(m.find(12) == kj::none);
  KJ_EXPECT(m.find(99) == kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(m.find(83)) == "bar");
  KJ_EXPECT(KJ_ASSERT_NONNULL(m.find(6)) == "qux");
  KJ_EXPECT(KJ_ASSERT_NONNULL(m.find(55)) == "corge");
}

KJ_TEST("HashMap eraseAll") {
  HashMap<int, StringPtr> m;
  testEraseAll(m);
}

KJ_TEST("TreeMap eraseAll") {
  TreeMap<int, StringPtr> m;
  testEraseAll(m);
}

KJ_TEST("HashMap<uint64> with int key") {
  // Make sure searching for an `int` key in a `uint64_t` table works -- i.e., the hashes are
  // consistent even though the types differ.
  kj::HashMap<uint64_t, kj::StringPtr> map;
  map.insert((uint64_t)123, "foo"_kj);
  KJ_EXPECT(KJ_ASSERT_NONNULL(map.find((int)123)) == "foo"_kj);

  // But also make sure that the upper bits of a 64-bit integer do affect the hash.
  KJ_EXPECT(kj::hashCode(0x1200000001ull) != kj::hashCode(0x3400000001ull));
  KJ_EXPECT(kj::hashCode(0x1200000001ull) != kj::hashCode(1));
}

}  // namespace
}  // namespace _
}  // namespace kj
