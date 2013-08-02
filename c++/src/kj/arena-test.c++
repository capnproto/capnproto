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
#include "thread.h"
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
    EXPECT_EQ(4u, arr1.size());
    EXPECT_EQ(2u, arr2.size());
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
    EXPECT_EQ(4u, arr1.size());
    EXPECT_EQ(2u, arr2.size());
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

  EXPECT_EQ(alignof(long) + sizeof(long), implicitCast<size_t>(&c2 - &c));
  EXPECT_EQ(alignof(long), implicitCast<size_t>(reinterpret_cast<char*>(&l) - &c));
  EXPECT_EQ(sizeof(char), implicitCast<size_t>(arr.begin() - &c2));
}

TEST(Arena, EndOfChunk) {
  union {
    byte scratch[64];
    uint64_t align;
  };
  Arena arena(arrayPtr(scratch, sizeof(scratch)));

  // First allocation will come from somewhere in the scratch space (after the chunk header).
  uint64_t& i = arena.allocate<uint64_t>();
  EXPECT_GE(reinterpret_cast<byte*>(&i), scratch);
  EXPECT_LT(reinterpret_cast<byte*>(&i), scratch + sizeof(scratch));

  // Next allocation will come at the next position.
  uint64_t& i2 = arena.allocate<uint64_t>();
  EXPECT_EQ(&i + 1, &i2);

  // Allocate the rest of the scratch space.
  size_t spaceLeft = scratch + sizeof(scratch) - reinterpret_cast<byte*>(&i2 + 1);
  ArrayPtr<byte> remaining = arena.allocateArray<byte>(spaceLeft);
  EXPECT_EQ(reinterpret_cast<byte*>(&i2 + 1), remaining.begin());

  // Next allocation comes from somewhere new.
  uint64_t& i3 = arena.allocate<uint64_t>();
  EXPECT_NE(remaining.end(), reinterpret_cast<byte*>(&i3));
}

TEST(Arena, EndOfChunkAlignment) {
  union {
    byte scratch[34];
    uint64_t align;
  };
  Arena arena(arrayPtr(scratch, sizeof(scratch)));

  // Figure out where we are...
  byte* start = arena.allocateArray<byte>(0).begin();

  // Allocate enough space so that we're 24 bytes into the scratch space.  (On 64-bit systems, this
  // should be zero.)
  arena.allocateArray<byte>(24 - (start - scratch));

  // Allocating a 16-bit integer works.  Now we're at 26 bytes; 8 bytes are left.
  uint16_t& i = arena.allocate<uint16_t>();
  EXPECT_EQ(scratch + 24, reinterpret_cast<byte*>(&i));

  // Although there is technically enough space to allocate a uint64, it is not aligned correctly,
  // so it will be allocated elsewhere instead.
  uint64_t& i2 = arena.allocate<uint64_t>();
  EXPECT_TRUE(reinterpret_cast<byte*>(&i2) < scratch ||
              reinterpret_cast<byte*>(&i2) > scratch + sizeof(scratch));
}

TEST(Arena, TooBig) {
  Arena arena(1024);

  byte& b1 = arena.allocate<byte>();

  ArrayPtr<byte> arr = arena.allocateArray<byte>(1024);

  byte& b2 = arena.allocate<byte>();

  // The array should not have been allocated anywhere near that first byte.
  EXPECT_TRUE(arr.begin() < &b1 || arr.begin() > &b1 + 512);

  // The next byte should have been allocated after the array.
  EXPECT_EQ(arr.end(), &b2);

  // Write to the array to make sure it's valid.
  memset(arr.begin(), 0xbe, arr.size());
}

TEST(Arena, MultiSegment) {
  // Sorry, this test makes assumptions about the size of ChunkHeader.
  Arena arena(sizeof(void*) == 4 ? 32 : 40);

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

  EXPECT_EQ(123u, arena.allocate<uint64_t>(123));
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

struct ThreadTestObject {
  ThreadTestObject* next;
  void* owner;  // points into the owning thread's stack

  ThreadTestObject(ThreadTestObject* next, void* owner)
      : next(next), owner(owner) {}
  ~ThreadTestObject() { ++destructorCount; }

  static uint destructorCount;
};
uint ThreadTestObject::destructorCount = 0;

TEST(Arena, Threads) {
  // Test thread-safety.  We allocate objects in four threads simultaneously, verify that they
  // are not corrupted, then verify that their destructors are all called when the Arena is
  // destroyed.

  {
    MutexGuarded<Arena> arena;

    // Func to run in each thread.
    auto threadFunc = [&]() {
      int me;
      ThreadTestObject* head = nullptr;

      {
        auto lock = arena.lockShared();

        // Allocate a huge linked list.
        for (uint i = 0; i < 100000; i++) {
          head = &lock->allocate<ThreadTestObject>(head, &me);
        }
      }

      // Wait until all other threads are done before verifying.
      arena.lockExclusive();

      // Verify that the list hasn't been corrupted.
      while (head != nullptr) {
        ASSERT_EQ(&me, head->owner);
        head = head->next;
      }
    };

    {
      auto lock = arena.lockExclusive();
      Thread thread1(threadFunc);
      Thread thread2(threadFunc);
      Thread thread3(threadFunc);
      Thread thread4(threadFunc);

      // Wait for threads to be ready.
      usleep(10000);

      auto release = kj::mv(lock);
      // As we go out of scope, the lock will be released (since `release` is destroyed first),
      // allowing all the threads to start running.  We'll then join each thread.
    }

    EXPECT_EQ(0u, ThreadTestObject::destructorCount);
  }

  EXPECT_EQ(400000u, ThreadTestObject::destructorCount);
}

}  // namespace
}  // namespace kj
