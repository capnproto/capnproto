// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "arena.h"
#include "debug.h"
#include <gtest/gtest.h>
#include <stdint.h>

namespace kj {
namespace {

struct TestObject {
  TestObject() {
    index = count;
    KJ_ASSERT(index != throwAt);
    ++count;
  }
  TestObject(const TestObject& other) {
    KJ_ASSERT(other.index != throwAt);
    index = -1;
    copiedCount++;
  }
  ~TestObject() noexcept(false) {
    if (index == -1) {
      --copiedCount;
    } else {
      --count;
      EXPECT_EQ(index, count);
      KJ_ASSERT(count != throwAt);
    }
  }

  int index;

  static int count;
  static int copiedCount;
  static int throwAt;
};

int TestObject::count = 0;
int TestObject::copiedCount = 0;
int TestObject::throwAt = -1;

TEST(Arena, Object) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  {
    Arena arena;

    TestObject& obj1 = arena.allocate<TestObject>();
    TestObject& obj2 = arena.allocate<TestObject>();

    EXPECT_LT(&obj1, &obj2);

    EXPECT_EQ(2, TestObject::count);
  }

  EXPECT_EQ(0, TestObject::count);
}

TEST(Arena, TrivialObject) {
  Arena arena;

  int& i1 = arena.allocate<int>();
  int& i2 = arena.allocate<int>();

  // Trivial objects should be tightly-packed.
  EXPECT_EQ(&i1 + 1, &i2);
}

TEST(Arena, OwnObject) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  Arena arena;

  {
    Own<TestObject> obj1 = arena.allocateOwn<TestObject>();
    Own<TestObject> obj2 = arena.allocateOwn<TestObject>();
    EXPECT_LT(obj1.get(), obj2.get());

    EXPECT_EQ(2, TestObject::count);
  }

  EXPECT_EQ(0, TestObject::count);
}

TEST(Arena, Array) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  {
    Arena arena;
    ArrayPtr<TestObject> arr1 = arena.allocateArray<TestObject>(4);
    ArrayPtr<TestObject> arr2 = arena.allocateArray<TestObject>(2);
    EXPECT_EQ(4, arr1.size());
    EXPECT_EQ(2, arr2.size());
    EXPECT_LE(arr1.end(), arr2.begin());
    EXPECT_EQ(6, TestObject::count);
  }

  EXPECT_EQ(0, TestObject::count);
}

TEST(Arena, TrivialArray) {
  Arena arena;
  ArrayPtr<int> arr1 = arena.allocateArray<int>(16);
  ArrayPtr<int> arr2 = arena.allocateArray<int>(8);

  // Trivial arrays should be tightly-packed.
  EXPECT_EQ(arr1.end(), arr2.begin());
}

TEST(Arena, OwnArray) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  Arena arena;

  {
    Array<TestObject> arr1 = arena.allocateOwnArray<TestObject>(4);
    Array<TestObject> arr2 = arena.allocateOwnArray<TestObject>(2);
    EXPECT_EQ(4, arr1.size());
    EXPECT_EQ(2, arr2.size());
    EXPECT_LE(arr1.end(), arr2.begin());
    EXPECT_EQ(6, TestObject::count);
  }

  EXPECT_EQ(0, TestObject::count);
}

#ifndef KJ_NO_EXCEPTIONS

TEST(Arena, ObjectThrow) {
  TestObject::count = 0;
  TestObject::throwAt = 1;

  {
    Arena arena;

    arena.allocate<TestObject>();
    EXPECT_ANY_THROW(arena.allocate<TestObject>());
    EXPECT_EQ(1, TestObject::count);
  }

  EXPECT_EQ(0, TestObject::count);
}

TEST(Arena, ArrayThrow) {
  TestObject::count = 0;
  TestObject::throwAt = 2;

  {
    Arena arena;
    EXPECT_ANY_THROW(arena.allocateArray<TestObject>(4));
    EXPECT_EQ(2, TestObject::count);
  }

  EXPECT_EQ(0, TestObject::count);
}

#endif

TEST(Arena, Alignment) {
  Arena arena;

  char& c = arena.allocate<char>();
  long& l = arena.allocate<long>();
  char& c2 = arena.allocate<char>();
  ArrayPtr<char> arr = arena.allocateArray<char>(8);

  EXPECT_EQ(alignof(long) + sizeof(long), &c2 - &c);
  EXPECT_EQ(alignof(long), reinterpret_cast<char*>(&l) - &c);
  EXPECT_EQ(sizeof(char), arr.begin() - &c2);
}

TEST(Arena, EndOfChunk) {
  union {
    byte scratch[16];
    uint64_t align;
  };
  Arena arena(arrayPtr(scratch, sizeof(scratch)));

  uint64_t& i = arena.allocate<uint64_t>();
  EXPECT_EQ(scratch, reinterpret_cast<byte*>(&i));

  uint64_t& i2 = arena.allocate<uint64_t>();
  EXPECT_EQ(scratch + 8, reinterpret_cast<byte*>(&i2));

  uint64_t& i3 = arena.allocate<uint64_t>();
  EXPECT_NE(scratch + 16, reinterpret_cast<byte*>(&i3));
}

TEST(Arena, EndOfChunkAlignment) {
  union {
    byte scratch[10];
    uint64_t align;
  };
  Arena arena(arrayPtr(scratch, sizeof(scratch)));

  uint16_t& i = arena.allocate<uint16_t>();
  EXPECT_EQ(scratch, reinterpret_cast<byte*>(&i));

  // Although there is technically enough space in the scratch array, it is not aligned correctly.
  uint64_t& i2 = arena.allocate<uint64_t>();
  EXPECT_TRUE(reinterpret_cast<byte*>(&i2) < scratch ||
              reinterpret_cast<byte*>(&i2) > scratch + sizeof(scratch));
}

TEST(Arena, TooBig) {
  Arena arena(1024);

  byte& b1 = arena.allocate<byte>();

  ArrayPtr<byte> arr = arena.allocateArray<byte>(1024);

  byte& b2 = arena.allocate<byte>();

  EXPECT_EQ(&b1 + 1, &b2);

  // Write to the array to make sure it's valid.
  memset(arr.begin(), 0xbe, arr.size());
}

TEST(Arena, MultiSegment) {
  Arena arena(24);

  uint64_t& i1 = arena.allocate<uint64_t>();
  uint64_t& i2 = arena.allocate<uint64_t>();
  uint64_t& i3 = arena.allocate<uint64_t>();

  EXPECT_EQ(&i1 + 1, &i2);
  EXPECT_NE(&i2 + 1, &i3);

  i1 = 1234;
  i2 = 5678;
  i3 = 9012;
}

TEST(Arena, Constructor) {
  Arena arena;

  EXPECT_EQ(123, arena.allocate<uint64_t>(123));
  EXPECT_EQ("foo", arena.allocate<StringPtr>("foo", 3));
}

TEST(Arena, Strings) {
  Arena arena;

  StringPtr foo = arena.copyString("foo");
  StringPtr bar = arena.copyString("bar");
  StringPtr quux = arena.copyString("quux");
  StringPtr corge = arena.copyString("corge");

  EXPECT_EQ("foo", foo);
  EXPECT_EQ("bar", bar);
  EXPECT_EQ("quux", quux);
  EXPECT_EQ("corge", corge);

  EXPECT_EQ(foo.end() + 1, bar.begin());
  EXPECT_EQ(bar.end() + 1, quux.begin());
  EXPECT_EQ(quux.end() + 1, corge.begin());
}

}  // namespace
}  // namespace kj
