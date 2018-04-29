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

#include "table.h"
#include <kj/test.h>

namespace kj {
namespace _ {

KJ_TEST("_::tryReserveSize() works") {
  {
    Vector<int> vec;
    tryReserveSize(vec, "foo"_kj);
    KJ_EXPECT(vec.capacity() == 3);
  }
  {
    Vector<int> vec;
    tryReserveSize(vec, 123);
    KJ_EXPECT(vec.capacity() == 0);
  }
}

class StringHasher {
public:
  bool matches(StringPtr a, StringPtr b) const {
    return a == b;
  }
  uint hashCode(StringPtr str) const {
    // djb hash with xor
    // TDOO(soon): Use KJ hash lib.
    size_t result = 5381;
    for (char c: str) {
      result = (result * 33) ^ c;
    }
    return result;
  }
};

KJ_TEST("simple table") {
  Table<StringPtr, HashIndex<StringHasher>> table;

  KJ_EXPECT(table.find("foo") == nullptr);

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert("foo") == "foo");
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert("bar") == "bar");
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("foo")) == "foo");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(table.find("fop") == nullptr);
  KJ_EXPECT(table.find("baq") == nullptr);

  {
    StringPtr& ref = table.insert("baz");
    KJ_EXPECT(ref == "baz");
    StringPtr& ref2 = KJ_ASSERT_NONNULL(table.find("baz"));
    KJ_EXPECT(&ref == &ref2);
  }

  KJ_EXPECT(table.size() == 3);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "foo");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(iter == table.end());
  }

  KJ_EXPECT(table.eraseMatch("foo"));
  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(table.find("foo") == nullptr);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(iter == table.end());
  }

  {
    auto& row = table.upsert("qux", [&](StringPtr&, StringPtr&&) {
      KJ_FAIL_ASSERT("shouldn't get here");
    });

    auto copy = kj::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(param.begin() == copy.begin());
      KJ_EXPECT(&existing == &row);
    });

    auto& found = KJ_ASSERT_NONNULL(table.find("qux"));
    KJ_EXPECT(&found == &row);
  }

  StringPtr STRS[] = { "corge"_kj, "grault"_kj, "garply"_kj };
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  KJ_EXPECT(table.size() == 6);
  KJ_EXPECT(table.find("corge") != nullptr);
  KJ_EXPECT(table.find("grault") != nullptr);
  KJ_EXPECT(table.find("garply") != nullptr);

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));

  KJ_EXPECT(table.size() == 6);

  KJ_EXPECT(table.insert("baa") == "baa");

  KJ_EXPECT(table.eraseAll([](StringPtr s) { return s.startsWith("ba"); }) == 3);
  KJ_EXPECT(table.size() == 4);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(iter == table.end());
  }
}

class BadHasher {
  // String hash that always returns the same hash code. This should not affect correctness, only
  // performance.
public:
  bool matches(StringPtr a, StringPtr b) const {
    return a == b;
  }
  uint hashCode(StringPtr str) const {
    return 1234;
  }
};

KJ_TEST("hash tables when hash is always same") {
  Table<StringPtr, HashIndex<BadHasher>> table;

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert("foo") == "foo");
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert("bar") == "bar");
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("foo")) == "foo");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(table.find("fop") == nullptr);
  KJ_EXPECT(table.find("baq") == nullptr);

  {
    StringPtr& ref = table.insert("baz");
    KJ_EXPECT(ref == "baz");
    StringPtr& ref2 = KJ_ASSERT_NONNULL(table.find("baz"));
    KJ_EXPECT(&ref == &ref2);
  }

  KJ_EXPECT(table.size() == 3);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "foo");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(iter == table.end());
  }

  KJ_EXPECT(table.eraseMatch("foo"));
  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(table.find("foo") == nullptr);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(iter == table.end());
  }

  {
    auto& row = table.upsert("qux", [&](StringPtr&, StringPtr&&) {
      KJ_FAIL_ASSERT("shouldn't get here");
    });

    auto copy = kj::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(param.begin() == copy.begin());
      KJ_EXPECT(&existing == &row);
    });

    auto& found = KJ_ASSERT_NONNULL(table.find("qux"));
    KJ_EXPECT(&found == &row);
  }

  StringPtr STRS[] = { "corge"_kj, "grault"_kj, "garply"_kj };
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  KJ_EXPECT(table.size() == 6);
  KJ_EXPECT(table.find("corge") != nullptr);
  KJ_EXPECT(table.find("grault") != nullptr);
  KJ_EXPECT(table.find("garply") != nullptr);

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));
}

struct SiPair {
  kj::StringPtr str;
  uint i;

  inline bool operator==(SiPair other) const {
    return str == other.str && i == other.i;
  }
};

class SiPairStringHasher {
public:
  bool matches(SiPair a, SiPair b) const {
    return a.str == b.str;
  }
  uint hashCode(SiPair pair) const {
    return inner.hashCode(pair.str);
  }

  bool matches(SiPair a, StringPtr b) const {
    return a.str == b;
  }
  uint hashCode(StringPtr str) const {
    return inner.hashCode(str);
  }

private:
  StringHasher inner;
};

class SiPairIntHasher {
public:
  bool matches(SiPair a, SiPair b) const {
    return a.i == b.i;
  }
  uint hashCode(SiPair pair) const {
    return pair.i;
  }

  bool matches(SiPair a, uint b) const {
    return a.i == b;
  }
  uint hashCode(uint i) const {
    return i;
  }
};

KJ_TEST("double-index table") {
  Table<SiPair, HashIndex<SiPairStringHasher>, HashIndex<SiPairIntHasher>> table;

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert({"foo", 123}) == (SiPair {"foo", 123}));
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert({"bar", 456}) == (SiPair {"bar", 456}));
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<HashIndex<SiPairStringHasher>>("foo")) ==
            (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<HashIndex<SiPairIntHasher>>(123)) ==
            (SiPair {"foo", 123}));

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(123)) == (SiPair {"foo", 123}));

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert({"foo", 111}));
  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert({"qux", 123}));

  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(123)) == (SiPair {"foo", 123}));
}

class UintHasher {
public:
  bool matches(uint a, uint b) const {
    return a == b;
  }
  uint hashCode(uint i) const {
    return i;
  }
};

KJ_TEST("large hash table") {
  constexpr uint SOME_PRIME = 6143;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    Table<uint, HashIndex<UintHasher>> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == nullptr);
      KJ_ASSERT(table.find(i * 5 + 124) == nullptr);
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(i * 5 + 123)));
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == nullptr);
      } else {
        uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

// =======================================================================================

KJ_TEST("B-tree internals") {
  {
    BTreeImpl::Leaf leaf;
    memset(&leaf, 0, sizeof(leaf));

    for (auto i: kj::indices(leaf.rows)) {
      KJ_CONTEXT(i);

      KJ_EXPECT(leaf.size() == i);

      if (i < kj::size(leaf.rows) / 2) {
#ifdef KJ_DEBUG
        KJ_EXPECT_THROW(FAILED, leaf.isHalfFull());
#endif
        KJ_EXPECT(!leaf.isMostlyFull());
      }

      if (i == kj::size(leaf.rows) / 2) {
        KJ_EXPECT(leaf.isHalfFull());
        KJ_EXPECT(!leaf.isMostlyFull());
      }

      if (i > kj::size(leaf.rows) / 2) {
        KJ_EXPECT(!leaf.isHalfFull());
        KJ_EXPECT(leaf.isMostlyFull());
      }

      if (i == kj::size(leaf.rows)) {
        KJ_EXPECT(leaf.isFull());
      } else {
        KJ_EXPECT(!leaf.isFull());
      }

      leaf.rows[i] = 1;
    }
    KJ_EXPECT(leaf.size() == kj::size(leaf.rows));
  }

  {
    BTreeImpl::Parent parent;
    memset(&parent, 0, sizeof(parent));

    for (auto i: kj::indices(parent.keys)) {
      KJ_CONTEXT(i);

      KJ_EXPECT(parent.keyCount() == i);

      if (i < kj::size(parent.keys) / 2) {
#ifdef KJ_DEBUG
        KJ_EXPECT_THROW(FAILED, parent.isHalfFull());
#endif
        KJ_EXPECT(!parent.isMostlyFull());
      }

      if (i == kj::size(parent.keys) / 2) {
        KJ_EXPECT(parent.isHalfFull());
        KJ_EXPECT(!parent.isMostlyFull());
      }

      if (i > kj::size(parent.keys) / 2) {
        KJ_EXPECT(!parent.isHalfFull());
        KJ_EXPECT(parent.isMostlyFull());
      }

      if (i == kj::size(parent.keys)) {
        KJ_EXPECT(parent.isFull());
      } else {
        KJ_EXPECT(!parent.isFull());
      }

      parent.keys[i] = 1;
    }
    KJ_EXPECT(parent.keyCount() == kj::size(parent.keys));
  }
}

class StringCompare {
public:
  bool isBefore(StringPtr a, StringPtr b) const {
    return a < b;
  }
  bool matches(StringPtr a, StringPtr b) const {
    return a == b;
  }
};

KJ_TEST("simple tree table") {
  Table<StringPtr, TreeIndex<StringCompare>> table;

  KJ_EXPECT(table.find("foo") == nullptr);

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert("foo") == "foo");
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert("bar") == "bar");
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("foo")) == "foo");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(table.find("fop") == nullptr);
  KJ_EXPECT(table.find("baq") == nullptr);

  {
    StringPtr& ref = table.insert("baz");
    KJ_EXPECT(ref == "baz");
    StringPtr& ref2 = KJ_ASSERT_NONNULL(table.find("baz"));
    KJ_EXPECT(&ref == &ref2);
  }

  KJ_EXPECT(table.size() == 3);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(*iter++ == "foo");
    KJ_EXPECT(iter == range.end());
  }

  KJ_EXPECT(table.eraseMatch("foo"));
  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(table.find("foo") == nullptr);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(iter == range.end());
  }

  {
    auto& row = table.upsert("qux", [&](StringPtr&, StringPtr&&) {
      KJ_FAIL_ASSERT("shouldn't get here");
    });

    auto copy = kj::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(param.begin() == copy.begin());
      KJ_EXPECT(&existing == &row);
    });

    auto& found = KJ_ASSERT_NONNULL(table.find("qux"));
    KJ_EXPECT(&found == &row);
  }

  StringPtr STRS[] = { "corge"_kj, "grault"_kj, "garply"_kj };
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  KJ_EXPECT(table.size() == 6);
  KJ_EXPECT(table.find("corge") != nullptr);
  KJ_EXPECT(table.find("grault") != nullptr);
  KJ_EXPECT(table.find("garply") != nullptr);

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));

  KJ_EXPECT(table.size() == 6);

  KJ_EXPECT(table.insert("baa") == "baa");

  KJ_EXPECT(table.eraseAll([](StringPtr s) { return s.startsWith("ba"); }) == 3);
  KJ_EXPECT(table.size() == 4);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(iter == range.end());
  }

  {
    auto range = table.range("foo", "har");
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(iter == range.end());
  }

  {
    auto range = table.range("garply", "grault");
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(iter == range.end());
  }
}

class UintCompare {
public:
  bool isBefore(uint a, uint b) const {
    return a < b;
  }
  bool matches(uint a, uint b) const {
    return a == b;
  }
};

KJ_TEST("large tree table") {
  constexpr uint SOME_PRIME = 6143;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    Table<uint, TreeIndex<UintCompare>> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == nullptr);
      KJ_ASSERT(table.find(i * 5 + 124) == nullptr);
    }
    table.verify();

    {
      auto range = table.ordered();
      auto iter = range.begin();
      for (uint i: kj::zeroTo(SOME_PRIME)) {
        KJ_ASSERT(*iter++ == i * 5 + 123);
      }
      KJ_ASSERT(iter == range.end());
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      KJ_CONTEXT(i);
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(i * 5 + 123), i));
        table.verify();
      }
    }

    {
      auto range = table.ordered();
      auto iter = range.begin();
      for (uint i: kj::zeroTo(SOME_PRIME)) {
        if (i % 2 == 0 || i % 7 == 0) {
          // erased
          KJ_ASSERT(table.find(i * 5 + 123) == nullptr);
        } else {
          uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
          KJ_ASSERT(value == i * 5 + 123);
          KJ_ASSERT(*iter++ == i * 5 + 123);
        }
      }
      KJ_ASSERT(iter == range.end());
    }
  }
}

}  // namespace kj
}  // namespace _
