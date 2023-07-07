// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "memory.h"
#include <kj/compat/gtest.h>
#include "debug.h"

namespace kj {
namespace {

TEST(Memory, OwnConst) {
  Own<int> i = heap<int>(2);
  EXPECT_EQ(2, *i);

  Own<const int> ci = mv(i);
  EXPECT_EQ(2, *ci);

  Own<const int> ci2 = heap<const int>(3);
  EXPECT_EQ(3, *ci2);
}

TEST(Memory, CanConvert) {
  struct Super { virtual ~Super() {} };
  struct Sub: public Super {};

  static_assert(canConvert<Own<Sub>, Own<Super>>(), "failure");
  static_assert(!canConvert<Own<Super>, Own<Sub>>(), "failure");
}

struct Nested {
  Nested(bool& destroyed): destroyed(destroyed) {}
  ~Nested() { destroyed = true; }

  bool& destroyed;
  Own<Nested> nested;
};

TEST(Memory, AssignNested) {
  bool destroyed1 = false, destroyed2 = false;
  auto nested = heap<Nested>(destroyed1);
  nested->nested = heap<Nested>(destroyed2);
  EXPECT_FALSE(destroyed1 || destroyed2);
  nested = kj::mv(nested->nested);
  EXPECT_TRUE(destroyed1);
  EXPECT_FALSE(destroyed2);
  nested = nullptr;
  EXPECT_TRUE(destroyed1 && destroyed2);
}

struct DestructionOrderRecorder {
  DestructionOrderRecorder(uint& counter, uint& recordTo)
      : counter(counter), recordTo(recordTo) {}
  ~DestructionOrderRecorder() {
    recordTo = ++counter;
  }

  uint& counter;
  uint& recordTo;
};

TEST(Memory, Attach) {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = kj::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = kj::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = kj::heap<DestructionOrderRecorder>(counter, destroyed3);

  auto ptr = obj1.get();

  Own<DestructionOrderRecorder> combined = obj1.attach(kj::mv(obj2), kj::mv(obj3));

  KJ_EXPECT(combined.get() == ptr);

  KJ_EXPECT(obj1.get() == nullptr);
  KJ_EXPECT(obj2.get() == nullptr);
  KJ_EXPECT(obj3.get() == nullptr);
  KJ_EXPECT(destroyed1 == 0);
  KJ_EXPECT(destroyed2 == 0);
  KJ_EXPECT(destroyed3 == 0);

  combined = nullptr;

  KJ_EXPECT(destroyed1 == 1, destroyed1);
  KJ_EXPECT(destroyed2 == 2, destroyed2);
  KJ_EXPECT(destroyed3 == 3, destroyed3);
}

TEST(Memory, AttachNested) {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = kj::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = kj::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = kj::heap<DestructionOrderRecorder>(counter, destroyed3);

  auto ptr = obj1.get();

  Own<DestructionOrderRecorder> combined = obj1.attach(kj::mv(obj2)).attach(kj::mv(obj3));

  KJ_EXPECT(combined.get() == ptr);

  KJ_EXPECT(obj1.get() == nullptr);
  KJ_EXPECT(obj2.get() == nullptr);
  KJ_EXPECT(obj3.get() == nullptr);
  KJ_EXPECT(destroyed1 == 0);
  KJ_EXPECT(destroyed2 == 0);
  KJ_EXPECT(destroyed3 == 0);

  combined = nullptr;

  KJ_EXPECT(destroyed1 == 1, destroyed1);
  KJ_EXPECT(destroyed2 == 2, destroyed2);
  KJ_EXPECT(destroyed3 == 3, destroyed3);
}

KJ_TEST("attachRef") {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = kj::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = kj::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = kj::heap<DestructionOrderRecorder>(counter, destroyed3);

  int i = 123;

  Own<int> combined = attachRef(i, kj::mv(obj1), kj::mv(obj2), kj::mv(obj3));

  KJ_EXPECT(combined.get() == &i);

  KJ_EXPECT(obj1.get() == nullptr);
  KJ_EXPECT(obj2.get() == nullptr);
  KJ_EXPECT(obj3.get() == nullptr);
  KJ_EXPECT(destroyed1 == 0);
  KJ_EXPECT(destroyed2 == 0);
  KJ_EXPECT(destroyed3 == 0);

  combined = nullptr;

  KJ_EXPECT(destroyed1 == 1, destroyed1);
  KJ_EXPECT(destroyed2 == 2, destroyed2);
  KJ_EXPECT(destroyed3 == 3, destroyed3);
}

KJ_TEST("attachVal") {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = kj::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = kj::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = kj::heap<DestructionOrderRecorder>(counter, destroyed3);

  int i = 123;

  Own<int> combined = attachVal(i, kj::mv(obj1), kj::mv(obj2), kj::mv(obj3));

  int* ptr = combined.get();
  KJ_EXPECT(ptr != &i);
  KJ_EXPECT(*ptr == i);

  KJ_EXPECT(obj1.get() == nullptr);
  KJ_EXPECT(obj2.get() == nullptr);
  KJ_EXPECT(obj3.get() == nullptr);
  KJ_EXPECT(destroyed1 == 0);
  KJ_EXPECT(destroyed2 == 0);
  KJ_EXPECT(destroyed3 == 0);

  combined = nullptr;

  KJ_EXPECT(destroyed1 == 1, destroyed1);
  KJ_EXPECT(destroyed2 == 2, destroyed2);
  KJ_EXPECT(destroyed3 == 3, destroyed3);
}

struct StaticType {
  int i;
};

struct DynamicType1 {
  virtual void foo() {}

  int j;

  DynamicType1(int j): j(j) {}
};

struct DynamicType2 {
  virtual void bar() {}

  int k;

  DynamicType2(int k): k(k) {}
};

struct SingularDerivedDynamic final: public DynamicType1 {
  SingularDerivedDynamic(int j, bool& destructorCalled)
      : DynamicType1(j), destructorCalled(destructorCalled) {}

  ~SingularDerivedDynamic() {
    destructorCalled = true;
  }
  KJ_DISALLOW_COPY_AND_MOVE(SingularDerivedDynamic);

  bool& destructorCalled;
};

struct MultipleDerivedDynamic final: public DynamicType1, public DynamicType2 {
  MultipleDerivedDynamic(int j, int k, bool& destructorCalled)
      : DynamicType1(j), DynamicType2(k), destructorCalled(destructorCalled) {}

  ~MultipleDerivedDynamic() {
    destructorCalled = true;
  }

  KJ_DISALLOW_COPY_AND_MOVE(MultipleDerivedDynamic);

  bool& destructorCalled;
};

TEST(Memory, OwnVoid) {
  {
    Own<StaticType> ptr = heap<StaticType>({123});
    StaticType* addr = ptr.get();
    Own<void> voidPtr = kj::mv(ptr);
    KJ_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<SingularDerivedDynamic> ptr = heap<SingularDerivedDynamic>(123, destructorCalled);
    SingularDerivedDynamic* addr = ptr.get();
    Own<void> voidPtr = kj::mv(ptr);
    KJ_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<void> voidPtr = kj::mv(ptr);
    KJ_EXPECT(voidPtr.get() == implicitCast<void*>(addr));

    KJ_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    KJ_EXPECT(destructorCalled);
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<DynamicType2> basePtr = kj::mv(ptr);
    DynamicType2* baseAddr = basePtr.get();

    // On most (all?) C++ ABIs, the second base class in a multiply-inherited class is offset from
    // the beginning of the object (assuming the first base class has non-zero size). We use this
    // fact here to verify that then casting to Own<void> does in fact result in a pointer that
    // points to the start of the overall object, not the base class. We expect that the pointers
    // are different here to prove that the test below is non-trivial.
    //
    // If there is some other ABI where these pointers are the same, and thus this expectation
    // fails, then it's no problem to #ifdef out the expectation on that platform.
    KJ_EXPECT(static_cast<void*>(baseAddr) != static_cast<void*>(addr));

    Own<void> voidPtr = kj::mv(basePtr);
    KJ_EXPECT(voidPtr.get() == static_cast<void*>(addr));

    KJ_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    KJ_EXPECT(destructorCalled);
  }

  {
    Maybe<Own<void>> maybe;
    maybe = Own<void>(&maybe, NullDisposer::instance);
    KJ_EXPECT(KJ_ASSERT_NONNULL(maybe).get() == &maybe);
    maybe = nullptr;
    KJ_EXPECT(maybe == nullptr);
  }
}

TEST(Memory, OwnConstVoid) {
  {
    Own<StaticType> ptr = heap<StaticType>({123});
    StaticType* addr = ptr.get();
    Own<const void> voidPtr = kj::mv(ptr);
    KJ_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<SingularDerivedDynamic> ptr = heap<SingularDerivedDynamic>(123, destructorCalled);
    SingularDerivedDynamic* addr = ptr.get();
    Own<const void> voidPtr = kj::mv(ptr);
    KJ_EXPECT(voidPtr.get() == implicitCast<void*>(addr));
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<const void> voidPtr = kj::mv(ptr);
    KJ_EXPECT(voidPtr.get() == implicitCast<void*>(addr));

    KJ_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    KJ_EXPECT(destructorCalled);
  }

  {
    bool destructorCalled = false;
    Own<MultipleDerivedDynamic> ptr = heap<MultipleDerivedDynamic>(123, 456, destructorCalled);
    MultipleDerivedDynamic* addr = ptr.get();
    Own<DynamicType2> basePtr = kj::mv(ptr);
    DynamicType2* baseAddr = basePtr.get();

    // On most (all?) C++ ABIs, the second base class in a multiply-inherited class is offset from
    // the beginning of the object (assuming the first base class has non-zero size). We use this
    // fact here to verify that then casting to Own<void> does in fact result in a pointer that
    // points to the start of the overall object, not the base class. We expect that the pointers
    // are different here to prove that the test below is non-trivial.
    //
    // If there is some other ABI where these pointers are the same, and thus this expectation
    // fails, then it's no problem to #ifdef out the expectation on that platform.
    KJ_EXPECT(static_cast<void*>(baseAddr) != static_cast<void*>(addr));

    Own<const void> voidPtr = kj::mv(basePtr);
    KJ_EXPECT(voidPtr.get() == static_cast<void*>(addr));

    KJ_EXPECT(!destructorCalled);
    voidPtr = nullptr;
    KJ_EXPECT(destructorCalled);
  }

  {
    Maybe<Own<const void>> maybe;
    maybe = Own<const void>(&maybe, NullDisposer::instance);
    KJ_EXPECT(KJ_ASSERT_NONNULL(maybe).get() == &maybe);
    maybe = nullptr;
    KJ_EXPECT(maybe == nullptr);
  }
}

struct IncompleteType;
KJ_DECLARE_NON_POLYMORPHIC(IncompleteType)

template <typename T, typename U>
struct IncompleteTemplate;
template <typename T, typename U>
KJ_DECLARE_NON_POLYMORPHIC(IncompleteTemplate<T, U>)

struct IncompleteDisposer: public Disposer {
  mutable void* sawPtr = nullptr;

  virtual void disposeImpl(void* pointer) const {
    sawPtr = pointer;
  }
};

KJ_TEST("Own<IncompleteType>") {
  static int i;
  void* ptr = &i;

  {
    IncompleteDisposer disposer;

    {
      kj::Own<IncompleteType> foo(reinterpret_cast<IncompleteType*>(ptr), disposer);
      kj::Own<IncompleteType> bar = kj::mv(foo);
    }

    KJ_EXPECT(disposer.sawPtr == ptr);
  }

  {
    IncompleteDisposer disposer;

    {
      kj::Own<IncompleteTemplate<int, char>> foo(
          reinterpret_cast<IncompleteTemplate<int, char>*>(ptr), disposer);
      kj::Own<IncompleteTemplate<int, char>> bar = kj::mv(foo);
    }

    KJ_EXPECT(disposer.sawPtr == ptr);
  }
}

KJ_TEST("Own with static disposer") {
  static int* disposedPtr = nullptr;
  struct MyDisposer {
    static void dispose(int* value) {
      KJ_EXPECT(disposedPtr == nullptr);
      disposedPtr = value;
    };
  };

  int i;

  {
    Own<int, MyDisposer> ptr(&i);
    KJ_EXPECT(disposedPtr == nullptr);
  }
  KJ_EXPECT(disposedPtr == &i);
  disposedPtr = nullptr;

  {
    Own<int, MyDisposer> ptr(&i);
    KJ_EXPECT(disposedPtr == nullptr);
    Own<int, MyDisposer> ptr2(kj::mv(ptr));
    KJ_EXPECT(disposedPtr == nullptr);
  }
  KJ_EXPECT(disposedPtr == &i);
  disposedPtr = nullptr;

  {
    Own<int, MyDisposer> ptr2;
    {
      Own<int, MyDisposer> ptr(&i);
      KJ_EXPECT(disposedPtr == nullptr);
      ptr2 = kj::mv(ptr);
      KJ_EXPECT(disposedPtr == nullptr);
    }
    KJ_EXPECT(disposedPtr == nullptr);
  }
  KJ_EXPECT(disposedPtr == &i);
}

KJ_TEST("Maybe<Own<T>>") {
  Maybe<Own<int>> m = heap<int>(123);
  KJ_EXPECT(m != nullptr);
  Maybe<int&> mRef = m;
  KJ_EXPECT(KJ_ASSERT_NONNULL(mRef) == 123);
  KJ_EXPECT(&KJ_ASSERT_NONNULL(mRef) == KJ_ASSERT_NONNULL(m).get());
}

#if __cplusplus > 201402L
int* sawIntPtr = nullptr;

void freeInt(int* ptr) {
  sawIntPtr = ptr;
  delete ptr;
}

void freeChar(char* c) {
  delete c;
}

void free(StaticType* ptr) {
  delete ptr;
}

void free(const char* ptr) {}

KJ_TEST("disposeWith") {
  auto i = new int(1);
  {
    auto p = disposeWith<freeInt>(i);
    KJ_EXPECT(sawIntPtr == nullptr);
  }
  KJ_EXPECT(sawIntPtr == i);
  {
    auto c = new char('a');
    auto p = disposeWith<freeChar>(c);
  }
  {
    // Explicit cast required to avoid ambiguity when overloads are present.
    auto s = new StaticType{1};
    auto p = disposeWith<static_cast<void(*)(StaticType*)>(free)>(s);
  }
  {
    const char c = 'a';
    auto p2 = disposeWith<static_cast<void(*)(const char*)>(free)>(&c);
  }
}
#endif

// TODO(test):  More tests.

}  // namespace
}  // namespace kj
