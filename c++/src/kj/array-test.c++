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

#include "array.h"
#include <gtest/gtest.h>
#include "debug.h"
#include <string>
#include <list>

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

struct TestNoexceptObject {
  TestNoexceptObject() noexcept {
    index = count;
    ++count;
  }
  TestNoexceptObject(const TestNoexceptObject& other) noexcept {
    index = -1;
    copiedCount++;
  }
  ~TestNoexceptObject() noexcept {
    if (index == -1) {
      --copiedCount;
    } else {
      --count;
      EXPECT_EQ(index, count);
    }
  }

  int index;

  static int count;
  static int copiedCount;
};

int TestNoexceptObject::count = 0;
int TestNoexceptObject::copiedCount = 0;

TEST(Array, TrivialConstructor) {
  char* ptr;
  {
    Array<char> chars = heapArray<char>(32);
    ptr = chars.begin();
    chars[0] = 12;
    chars[1] = 34;
  }

  {
    Array<char> chars = heapArray<char>(32);

    // TODO(test):  The following doesn't work in opt mode -- I guess some allocators zero the
    //   memory?  Is there some other way we can test this?  Maybe override malloc()?
//    // Somewhat hacky:  We can't guarantee that the new array is allocated in the same place, but
//    // any reasonable allocator is highly likely to do so.  If it does, then we expect that the
//    // memory has not been initialized.
//    if (chars.begin() == ptr) {
//      EXPECT_NE(chars[0], 0);
//      EXPECT_NE(chars[1], 0);
//    }
  }
}

TEST(Array, ComplexConstructor) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  {
    Array<TestObject> array = heapArray<TestObject>(32);
    EXPECT_EQ(32, TestObject::count);
  }
  EXPECT_EQ(0, TestObject::count);
}

#if !KJ_NO_EXCEPTIONS
TEST(Array, ThrowingConstructor) {
  TestObject::count = 0;
  TestObject::throwAt = 16;

  // If a constructor throws, the previous elements should still be destroyed.
  EXPECT_ANY_THROW(heapArray<TestObject>(32));
  EXPECT_EQ(0, TestObject::count);
}

TEST(Array, ThrowingDestructor) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  Array<TestObject> array = heapArray<TestObject>(32);
  EXPECT_EQ(32, TestObject::count);

  // If a destructor throws, all elements should still be destroyed.
  TestObject::throwAt = 16;
  EXPECT_ANY_THROW(array = nullptr);
  EXPECT_EQ(0, TestObject::count);
}
#endif  // !KJ_NO_EXCEPTIONS

TEST(Array, AraryBuilder) {
  TestObject::count = 0;
  TestObject::throwAt = -1;

  Array<TestObject> array;

  {
    ArrayBuilder<TestObject> builder = heapArrayBuilder<TestObject>(32);

    for (int i = 0; i < 32; i++) {
      EXPECT_EQ(i, TestObject::count);
      builder.add();
    }

    EXPECT_EQ(32, TestObject::count);
    array = builder.finish();
    EXPECT_EQ(32, TestObject::count);
  }

  EXPECT_EQ(32, TestObject::count);
  array = nullptr;
  EXPECT_EQ(0, TestObject::count);
}

TEST(Array, AraryBuilderAddAll) {
  {
    // Trivial case.
    char text[] = "foo";
    ArrayBuilder<char> builder = heapArrayBuilder<char>(5);
    builder.add('<');
    builder.addAll(text, text + 3);
    builder.add('>');
    auto array = builder.finish();
    EXPECT_EQ("<foo>", std::string(array.begin(), array.end()));
  }

  {
    // Trivial case, const.
    const char* text = "foo";
    ArrayBuilder<char> builder = heapArrayBuilder<char>(5);
    builder.add('<');
    builder.addAll(text, text + 3);
    builder.add('>');
    auto array = builder.finish();
    EXPECT_EQ("<foo>", std::string(array.begin(), array.end()));
  }

  {
    // Trivial case, non-pointer iterator.
    std::list<char> text = {'f', 'o', 'o'};
    ArrayBuilder<char> builder = heapArrayBuilder<char>(5);
    builder.add('<');
    builder.addAll(text);
    builder.add('>');
    auto array = builder.finish();
    EXPECT_EQ("<foo>", std::string(array.begin(), array.end()));
  }

  {
    // Complex case.
    std::string strs[] = {"foo", "bar", "baz"};
    ArrayBuilder<std::string> builder = heapArrayBuilder<std::string>(5);
    builder.add("qux");
    builder.addAll(strs, strs + 3);
    builder.add("quux");
    auto array = builder.finish();
    EXPECT_EQ("qux", array[0]);
    EXPECT_EQ("foo", array[1]);
    EXPECT_EQ("bar", array[2]);
    EXPECT_EQ("baz", array[3]);
    EXPECT_EQ("quux", array[4]);
  }

  {
    // Complex case, noexcept.
    TestNoexceptObject::count = 0;
    TestNoexceptObject::copiedCount = 0;
    TestNoexceptObject objs[3];
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(0, TestNoexceptObject::copiedCount);
    ArrayBuilder<TestNoexceptObject> builder = heapArrayBuilder<TestNoexceptObject>(3);
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(0, TestNoexceptObject::copiedCount);
    builder.addAll(objs, objs + 3);
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(3, TestNoexceptObject::copiedCount);
    auto array = builder.finish();
    EXPECT_EQ(3, TestNoexceptObject::count);
    EXPECT_EQ(3, TestNoexceptObject::copiedCount);
  }
  EXPECT_EQ(0, TestNoexceptObject::count);
  EXPECT_EQ(0, TestNoexceptObject::copiedCount);

  {
    // Complex case, exceptions possible.
    TestObject::count = 0;
    TestObject::copiedCount = 0;
    TestObject::throwAt = -1;
    TestObject objs[3];
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);
    ArrayBuilder<TestObject> builder = heapArrayBuilder<TestObject>(3);
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);
    builder.addAll(objs, objs + 3);
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(3, TestObject::copiedCount);
    auto array = builder.finish();
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(3, TestObject::copiedCount);
  }
  EXPECT_EQ(0, TestObject::count);
  EXPECT_EQ(0, TestObject::copiedCount);

#if !KJ_NO_EXCEPTIONS
  {
    // Complex case, exceptions occur.
    TestObject::count = 0;
    TestObject::copiedCount = 0;
    TestObject::throwAt = -1;
    TestObject objs[3];
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);

    TestObject::throwAt = 1;

    ArrayBuilder<TestObject> builder = heapArrayBuilder<TestObject>(3);
    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);

    EXPECT_ANY_THROW(builder.addAll(objs, objs + 3));
    TestObject::throwAt = -1;

    EXPECT_EQ(3, TestObject::count);
    EXPECT_EQ(0, TestObject::copiedCount);
  }
  EXPECT_EQ(0, TestObject::count);
  EXPECT_EQ(0, TestObject::copiedCount);
#endif  // !KJ_NO_EXCEPTIONS
}

TEST(Array, HeapCopy) {
  {
    Array<char> copy = heapArray("foo", 3);
    EXPECT_EQ(3u, copy.size());
    EXPECT_EQ("foo", std::string(copy.begin(), 3));
  }
  {
    Array<char> copy = heapArray(ArrayPtr<const char>("bar", 3));
    EXPECT_EQ(3u, copy.size());
    EXPECT_EQ("bar", std::string(copy.begin(), 3));
  }
  {
    const char* ptr = "baz";
    Array<char> copy = heapArray<char>(ptr, ptr + 3);
    EXPECT_EQ(3u, copy.size());
    EXPECT_EQ("baz", std::string(copy.begin(), 3));
  }
}

TEST(Array, OwnConst) {
  ArrayBuilder<int> builder = heapArrayBuilder<int>(2);
  int x[2] = {123, 234};
  builder.addAll(x, x + 2);

  Array<int> i = builder.finish(); //heapArray<int>({123, 234});
  ASSERT_EQ(2u, i.size());
  EXPECT_EQ(123, i[0]);
  EXPECT_EQ(234, i[1]);

  Array<const int> ci = mv(i);
  ASSERT_EQ(2u, ci.size());
  EXPECT_EQ(123, ci[0]);
  EXPECT_EQ(234, ci[1]);

  Array<const int> ci2 = heapArray<const int>({345, 456});
  ASSERT_EQ(2u, ci2.size());
  EXPECT_EQ(345, ci2[0]);
  EXPECT_EQ(456, ci2[1]);
}

}  // namespace
}  // namespace kj
