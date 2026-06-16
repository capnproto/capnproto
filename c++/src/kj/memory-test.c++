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

#include "kj/string.h"
#include "kj/refcount.h"
#include "kj/test.h"
#include "function.h"
#include "memory.h"
#include <signal.h>
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

KJ_TEST("attach Refcounted") {
  {
    struct RcDerived: public Refcounted {};
    struct RcDerived2: public RcDerived {};
    struct ArcDerived: public AtomicRefcounted {};
    struct ArcDerived2: public ArcDerived {};

    auto obj1 = kj::refcounted<Refcounted>();
    auto obj2 = kj::refcounted<RcDerived>();
    auto obj3 = kj::refcounted<RcDerived2>();
    auto obj4 = kj::atomicRefcounted<AtomicRefcounted>();
    auto obj5 = kj::atomicRefcounted<ArcDerived>();
    auto obj6 = kj::atomicRefcounted<ArcDerived2>();

#if 0
    // Manually observed that these trigger output a deprecation warning during compilation, but
    // need to disable their compilation, since the CI build forbids deprecation warnings.
    obj1 = obj1.attach(kj::heap<bool>());
    obj2 = obj2.attach(kj::heap<bool>());
    obj3 = obj3.attach(kj::heap<bool>());
    obj4 = obj4.attach(kj::heap<bool>());
    obj5 = obj5.attach(kj::heap<bool>());
    obj6 = obj6.attach(kj::heap<bool>());
#endif

    // No deprecation warning:
    obj1 = obj1.attachToThisReference(kj::heap<bool>());
    obj2 = obj2.attachToThisReference(kj::heap<bool>());
    obj3 = obj3.attachToThisReference(kj::heap<bool>());
    obj4 = obj4.attachToThisReference(kj::heap<bool>());
    obj5 = obj5.attachToThisReference(kj::heap<bool>());
    obj6 = obj6.attachToThisReference(kj::heap<bool>());
  }

  // Confirming attachToThisReference() works similarly to attach():
  {
    uint counter = 0;
    uint destroyed1 = 0;
    uint destroyed2 = 0;
    uint destroyed3 = 0;
    uint destroyed4 = 0;
    uint destroyed5 = 0;
    uint destroyed6 = 0;

    auto obj1 = kj::heap<DestructionOrderRecorder>(counter, destroyed1);
    auto obj2 = kj::heap<DestructionOrderRecorder>(counter, destroyed2);
    auto obj3 = kj::heap<DestructionOrderRecorder>(counter, destroyed3);
    auto obj4 = kj::heap<DestructionOrderRecorder>(counter, destroyed4);
    auto obj5 = kj::heap<DestructionOrderRecorder>(counter, destroyed5);
    auto obj6 = kj::heap<DestructionOrderRecorder>(counter, destroyed6);
    auto combined = kj::refcounted<Refcounted>().attachToThisReference(kj::mv(obj1), kj::mv(obj2),
        kj::mv(obj3));
    auto otherRef = kj::addRef(*combined).attachToThisReference(kj::mv(obj4), kj::mv(obj5),
        kj::mv(obj6));

    KJ_EXPECT(combined.get() == otherRef.get());
    KJ_EXPECT(destroyed1 == 0);
    KJ_EXPECT(destroyed2 == 0);
    KJ_EXPECT(destroyed3 == 0);
    KJ_EXPECT(destroyed4 == 0);
    KJ_EXPECT(destroyed5 == 0);
    KJ_EXPECT(destroyed6 == 0);

    combined = nullptr;

    KJ_EXPECT(destroyed1 == 1);
    KJ_EXPECT(destroyed2 == 2);
    KJ_EXPECT(destroyed3 == 3);
    KJ_EXPECT(destroyed4 == 0);
    KJ_EXPECT(destroyed5 == 0);
    KJ_EXPECT(destroyed6 == 0);

    otherRef = nullptr;

    KJ_EXPECT(destroyed1 == 1);
    KJ_EXPECT(destroyed2 == 2);
    KJ_EXPECT(destroyed3 == 3);
    KJ_EXPECT(destroyed4 == 4);
    KJ_EXPECT(destroyed5 == 5);
    KJ_EXPECT(destroyed6 == 6);
  }
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
    maybe = kj::none;
    KJ_EXPECT(maybe == kj::none);
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
    maybe = kj::none;
    KJ_EXPECT(maybe == kj::none);
  }

  {
    bool destructorCalled = false;
    Own<SingularDerivedDynamic> ptr = heap<SingularDerivedDynamic>(123, destructorCalled);
    SingularDerivedDynamic* addr = ptr.get();

    KJ_EXPECT(ptr.disown(&_::HeapDisposer<SingularDerivedDynamic>::instance) == addr);
    KJ_EXPECT(!destructorCalled);
    ptr = nullptr;
    KJ_EXPECT(!destructorCalled);

    _::HeapDisposer<SingularDerivedDynamic>::instance.dispose(addr);
    KJ_EXPECT(destructorCalled);
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

  void disposeImpl(void* pointer) const override {
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
  KJ_EXPECT(m != kj::none);
  Maybe<int&> mRef = m;
  KJ_EXPECT(KJ_ASSERT_NONNULL(mRef) == 123);
  KJ_EXPECT(&KJ_ASSERT_NONNULL(mRef) == KJ_ASSERT_NONNULL(m).get());
}

KJ_TEST("Maybe<Own<T>> comprehensive") {
  // Maybe<Own<T>> currently has a partial specialization in memory.h. These tests document its
  // behavior comprehensively to ensure any future refactoring preserves the expected semantics.

  // Size verification: Maybe<Own<T>> should have no overhead beyond Own<T> itself, since
  // nullptr can represent the empty state.
  static_assert(sizeof(Maybe<Own<int>>) == sizeof(Own<int>));

  // Default construction (empty)
  {
    Maybe<Own<int>> empty;
    KJ_EXPECT(empty == kj::none);
  }

  // Construction from kj::none
  {
    Maybe<Own<int>> empty = kj::none;
    KJ_EXPECT(empty == kj::none);
  }

  // Construction from Own<T>
  {
    Maybe<Own<int>> m = heap<int>(42);
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m) == 42);
  }

  // Move construction
  {
    Maybe<Own<int>> m1 = heap<int>(42);
    Maybe<Own<int>> m2 = kj::mv(m1);
    KJ_EXPECT(m1 == kj::none);  // moved-from is empty
    KJ_EXPECT(m2 != kj::none);
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m2) == 42);
  }

  // Move assignment
  {
    Maybe<Own<int>> m1 = heap<int>(1);
    Maybe<Own<int>> m2 = heap<int>(2);
    m1 = kj::mv(m2);
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m1) == 2);
    KJ_EXPECT(m2 == kj::none);
  }

  // Assignment of Own<T>
  {
    Maybe<Own<int>> m;
    m = heap<int>(42);
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m) == 42);
  }

  // Assignment of kj::none
  {
    Maybe<Own<int>> m = heap<int>(42);
    m = kj::none;
    KJ_EXPECT(m == kj::none);
  }

  // KJ_IF_SOME with mutable access
  {
    Maybe<Own<int>> m = heap<int>(42);
    KJ_IF_SOME(own, m) {
      KJ_EXPECT(*own == 42);
      *own = 100;  // mutate through reference
    } else {
      KJ_FAIL_EXPECT("expected non-null");
    }
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m) == 100);
  }

  // KJ_IF_SOME on empty
  {
    Maybe<Own<int>> empty;
    KJ_IF_SOME(own, empty) {
      (void)own;
      KJ_FAIL_EXPECT("expected null");
    }
  }

  // Rvalue KJ_IF_SOME (move out)
  {
    Maybe<Own<int>> m = heap<int>(42);
    KJ_IF_SOME(own, kj::mv(m)) {
      KJ_EXPECT(*own == 42);
      Own<int> taken = kj::mv(own);  // take ownership
      KJ_EXPECT(*taken == 42);
    }
    KJ_EXPECT(m == kj::none);
  }

  // map() on lvalue
  {
    Maybe<Own<int>> m = heap<int>(42);
    Maybe<int> mapped = m.map([](Own<int>& o) { return *o * 2; });
    KJ_EXPECT(KJ_ASSERT_NONNULL(mapped) == 84);
  }

  // map() on empty
  {
    Maybe<Own<int>> empty;
    Maybe<int> mapped = empty.map([](Own<int>& o) { return *o; });
    KJ_EXPECT(mapped == kj::none);
  }

  // map() on rvalue (moving out)
  {
    Maybe<Own<int>> m = heap<int>(42);
    Maybe<Own<int>> mapped = kj::mv(m).map([](Own<int>&& o) { return kj::mv(o); });
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(*KJ_ASSERT_NONNULL(mapped) == 42);
  }

  // orDefault() with lvalue
  {
    Maybe<Own<int>> m = heap<int>(42);
    Own<int> def = heap<int>(0);
    KJ_EXPECT(*m.orDefault(def) == 42);
  }

  // orDefault() on empty
  {
    Maybe<Own<int>> empty;
    Own<int> def = heap<int>(99);
    KJ_EXPECT(*empty.orDefault(def) == 99);
  }

  // Lazy orDefault on rvalue
  {
    Maybe<Own<int>> empty;
    Own<int> result = kj::mv(empty).orDefault([]() { return heap<int>(123); });
    KJ_EXPECT(*result == 123);
  }

  // emplace()
  {
    Maybe<Own<int>> m;
    m.emplace(heap<int>(42));
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m) == 42);
  }

  // Disposal: destroyed when Maybe goes out of scope
  {
    bool destroyed = false;
    struct Guard {
      bool& flag;
      Guard(bool& f) : flag(f) {}
      ~Guard() { flag = true; }
    };
    {
      Maybe<Own<Guard>> m = heap<Guard>(destroyed);
      KJ_EXPECT(!destroyed);
    }
    KJ_EXPECT(destroyed);
  }

  // Disposal: destroyed when set to none
  {
    bool destroyed = false;
    struct Guard {
      bool& flag;
      Guard(bool& f) : flag(f) {}
      ~Guard() { flag = true; }
    };
    Maybe<Own<Guard>> m = heap<Guard>(destroyed);
    m = kj::none;
    KJ_EXPECT(destroyed);
  }

  // Disposal: old value destroyed on reassignment
  {
    bool destroyed1 = false, destroyed2 = false;
    struct Guard {
      bool& flag;
      Guard(bool& f) : flag(f) {}
      ~Guard() { flag = true; }
    };
    Maybe<Own<Guard>> m = heap<Guard>(destroyed1);
    m = heap<Guard>(destroyed2);
    KJ_EXPECT(destroyed1);  // old value destroyed on reassignment
    KJ_EXPECT(!destroyed2); // new value still alive
  }
}

KJ_TEST("Maybe<Own<T>> move-assignment is safe when this owns other") {
  // Test that move-assignment works correctly when `other` is inside `this`'s value.
  // This is a regression test for a use-after-free bug where:
  //   head = kj::mv(head->next);
  // would access head->next after head's value was destroyed.

  struct ListNode {
    int value;
    Maybe<Own<ListNode>> next;
    ListNode(int v): value(v) {}
  };

  // Build a list: 1 -> 2 -> 3 -> none
  Maybe<Own<ListNode>> head = heap<ListNode>(1);
  KJ_ASSERT_NONNULL(head)->next = heap<ListNode>(2);
  KJ_ASSERT_NONNULL(KJ_ASSERT_NONNULL(head)->next)->next = heap<ListNode>(3);

  // Pop the head by assigning head = kj::mv(head->next)
  // Without a correctly implemented assignment operator, this would be use-after-free.
  KJ_IF_SOME(node, head) {
    head = kj::mv(node->next);
  }

  KJ_EXPECT(head != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(head)->value == 2);

  // Pop again
  KJ_IF_SOME(node, head) {
    head = kj::mv(node->next);
  }

  KJ_EXPECT(head != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(head)->value == 3);

  // Pop once more - should become none
  KJ_IF_SOME(node, head) {
    head = kj::mv(node->next);
  }

  KJ_EXPECT(head == kj::none);
}

KJ_TEST("Maybe<Own<T>> implicit conversion to Maybe<T&>") {
  // Maybe<Own<T>> can implicitly convert to Maybe<T&>, which is useful for
  // passing owned values to functions expecting references.

  // Lvalue conversion
  {
    Maybe<Own<int>> m = heap<int>(42);
    Maybe<int&> ref = m;
    KJ_EXPECT(ref != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(ref) == 42);

    // Modifying through the reference affects the owned value
    KJ_ASSERT_NONNULL(ref) = 100;
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m) == 100);
  }

  // Rvalue conversion (safe for function call lifetime)
  {
    auto takesRef = [](Maybe<int&> ref) {
      KJ_EXPECT(KJ_ASSERT_NONNULL(ref) == 42);
    };
    takesRef(Maybe<Own<int>>(heap<int>(42)));
  }

  // Empty conversion
  {
    Maybe<Own<int>> empty;
    Maybe<int&> ref = empty;
    KJ_EXPECT(ref == kj::none);
  }

  // Const conversion
  {
    const Maybe<Own<int>> m = heap<int>(42);
    Maybe<const int&> ref = m;
    KJ_EXPECT(KJ_ASSERT_NONNULL(ref) == 42);
  }

  // Function parameter passing (common use case)
  {
    auto processValue = [](Maybe<int&> ref) -> bool {
      KJ_IF_SOME(v, ref) {
        v *= 2;
        return true;
      }
      return false;
    };

    Maybe<Own<int>> m = heap<int>(21);
    KJ_EXPECT(processValue(m));
    KJ_EXPECT(*KJ_ASSERT_NONNULL(m) == 42);

    Maybe<Own<int>> empty;
    KJ_EXPECT(!processValue(empty));
  }
}

// Types for testing Maybe<Own<Derived>> -> Maybe<Own<Base>> converting constructor
struct MaybeOwnBase {
  int value;
  explicit MaybeOwnBase(int v): value(v) {}
  virtual ~MaybeOwnBase() = default;
};

struct MaybeOwnDerived: MaybeOwnBase {
  explicit MaybeOwnDerived(int v): MaybeOwnBase(v) {}
};

KJ_TEST("Maybe<Own<T>> converting constructor from Own<Derived>") {
  // Test that Own<Derived> can be used to construct Maybe<Own<Base>>.

  // Direct construction from heap<Derived>()
  {
    Maybe<Own<MaybeOwnBase>> m = heap<MaybeOwnDerived>(42);
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 42);
  }

  // Assignment from heap<Derived>()
  {
    Maybe<Own<MaybeOwnBase>> m;
    m = heap<MaybeOwnDerived>(42);
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 42);
  }

  // Return from function without braces
  {
    auto makeWidget = []() -> Maybe<Own<MaybeOwnBase>> {
      return heap<MaybeOwnDerived>(42);  // No braces needed
    };
    Maybe<Own<MaybeOwnBase>> m = makeWidget();
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 42);
  }

  // Move from existing Own<Derived>
  {
    Own<MaybeOwnDerived> owned = heap<MaybeOwnDerived>(42);
    Maybe<Own<MaybeOwnBase>> m = kj::mv(owned);
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 42);
  }
}

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

// TODO(test):  More tests.

struct Obj {
  Obj(kj::StringPtr name) : name(kj::str(name)) { }
  Obj(Obj&&) = default;

  kj::String name;

  KJ_DISALLOW_COPY(Obj);
};

struct PtrHolder {
  kj::Ptr<Obj> ptr;
};

KJ_TEST("kj::Pin<T> basic properties") {
  // kj::Pin<T> guarantees that T won't move or disappear while there are active pointers.
  
  // pin constructor is a simple argument pass through
  kj::Pin<Obj> pin("a");

  // pin is a smart pointer and can be used so
  KJ_EXPECT(pin->name == "a"_kj);

  // pin can be auto converted to Ptr<T>
  kj::Ptr<Obj> ptr1 = pin;
  KJ_EXPECT(ptr1 == pin);
  KJ_EXPECT(pin == ptr1);

  // Ptr<T> is a smart pointer too
  KJ_EXPECT(ptr1->name == "a"_kj);

  // you can have more than one Ptr<T> pointing to the same object
  kj::Ptr<Obj> ptr2 = pin;
  KJ_EXPECT(ptr1 == ptr2);
  KJ_EXPECT(ptr2->name == "a"_kj);

  // when leaving the scope ptrs will be destroyed first,
  // so pin will be destroyed without problems
}

KJ_TEST("moving kj::Pin<T>") {
  kj::Pin<Obj> pin("a");

  // you can move pin around as long as there are no pointers to it
  kj::Pin<Obj> pin2(kj::mv(pin));
  
  // data belongs to a new pin now
  KJ_EXPECT(pin2->name == "a"_kj);

  // it is C++ and old pin still points to a valid object
  KJ_EXPECT(pin->name == ""_kj);

  // you can add new pointers to the pin with asPtr() method as well
  kj::Ptr<Obj> ptr1 = pin2.asPtr();
  KJ_EXPECT(ptr1 == pin2);
  KJ_EXPECT(ptr1->name == "a"_kj);

  {
    // you can copy pointers
    kj::Ptr<Obj> ptr2 = ptr1;
    KJ_EXPECT(ptr2 == ptr1);
    KJ_EXPECT(ptr2->name == "a"_kj);

    // ptr2 will be auto-destroyed
  }

  // you can move the pin again if all pointers are destroyed
  ptr1 = nullptr;
  kj::Pin<Obj> pin3(kj::mv(pin2));
  KJ_EXPECT(pin3->name == "a"_kj);
}

struct Obj2 : public Obj {
  Obj2(kj::StringPtr name, int size) : Obj(name), size(size) {}
  int size;
};

struct OtherBase {
  int other = 123;
};

struct MultiBaseObj2 : public OtherBase, public Obj2 {
  MultiBaseObj2(kj::StringPtr name, int size) : Obj2(name, size) {}
};

KJ_TEST("kj::Ptr<T> subtyping") {
  // pin the child
  kj::Pin<Obj2> pin("obj2", 42);

  // pointer to the child
  kj::Ptr<Obj2> ptr1 = pin;
  KJ_EXPECT(ptr1->name == "obj2"_kj);
  KJ_EXPECT(ptr1->size == 42);

  // pointer to the base
  kj::Ptr<Obj> ptr2 = pin;
  KJ_EXPECT(ptr2->name == "obj2"_kj);
  KJ_EXPECT(ptr2 == pin);
  KJ_EXPECT(ptr1 == ptr2);

  // pointers can be copied or moved to the base type too
  kj::Ptr<Obj> ptr3 = ptr1;
  KJ_EXPECT(ptr3->name == "obj2"_kj);
  KJ_EXPECT(ptr3 == pin);

  kj::Ptr<Obj> ptr4 = kj::mv(ptr1);
  KJ_EXPECT(ptr4->name == "obj2"_kj);
  KJ_EXPECT(ptr4 == pin);

  kj::Ptr<const Obj2> ptr5 = pin;
  KJ_EXPECT(ptr5->name == "obj2"_kj);
}

KJ_TEST("kj::Weak<T> subtyping") {
  static_assert(kj::canConvert<kj::Weak<Obj2>, kj::Weak<Obj>>(), "failure");
  static_assert(!kj::canConvert<kj::Weak<Obj>, kj::Weak<Obj2>>(), "failure");
  static_assert(kj::canConvert<kj::Weak<Obj2>, kj::Weak<const Obj>>(), "failure");
  static_assert(kj::MaybeTraits<kj::Weak<Obj>>::convertingConstructor,
      "Maybe<Weak<T>> should opt into converting construction");
  static_assert(kj::canConvert<kj::Weak<Obj2>, kj::Maybe<kj::Weak<Obj>>>(),
      "Maybe<Weak<Base>> should be implicitly constructible from Weak<Derived>");
  static_assert(!kj::canConvert<kj::Weak<Obj>, kj::Maybe<kj::Weak<Obj2>>>(),
      "Maybe<Weak<Derived>> should not be implicitly constructible from Weak<Base>");

  kj::Pin<Obj2> pin("obj2", 42);
  kj::Weak<Obj2> weak1 = pin.addWeak();
  kj::Weak<Obj> weak2 = weak1;
  KJ_EXPECT(weak2 == pin);
  KJ_EXPECT(weak2.assertLive().name == "obj2"_kj);

  kj::Weak<const Obj> weak3 = weak1;
  KJ_EXPECT(weak3.assertLive().name == "obj2"_kj);

  kj::Maybe<kj::Weak<Obj>> maybeWeak = weak1;
  KJ_IF_SOME(weak, maybeWeak) {
    KJ_EXPECT(weak == pin);
    KJ_EXPECT(weak.assertLive().name == "obj2"_kj);
  } else {
    KJ_FAIL_EXPECT("expected Maybe<Weak<T>> to contain a pointer");
  }

  kj::Pin<MultiBaseObj2> multiPin("multi", 123);
  kj::Weak<MultiBaseObj2> multiWeak = multiPin.addWeak();
  kj::Weak<Obj> baseWeak = multiWeak;
  KJ_EXPECT(baseWeak.assertLive().name == "multi"_kj);
  KJ_IF_SOME(basePtr, baseWeak) {
    KJ_EXPECT(basePtr->name == "multi"_kj);
  } else {
    KJ_FAIL_EXPECT("expected Weak<Base> to upgrade");
  }

  kj::Pin<MultiBaseObj2> movedMultiPin(kj::mv(multiPin));
  KJ_EXPECT(baseWeak.tryGet() == kj::none);
  KJ_EXPECT(baseWeak.upgrade() == kj::none);

  kj::Weak<Obj> movedBaseWeak = movedMultiPin.addWeak();
  KJ_EXPECT(movedBaseWeak == movedMultiPin);
  KJ_EXPECT(movedBaseWeak.assertLive().name == "multi"_kj);
  KJ_IF_SOME(basePtr, movedBaseWeak) {
    KJ_EXPECT(basePtr->name == "multi"_kj);
  } else {
    KJ_FAIL_EXPECT("expected new Weak<Base> to upgrade after move");
  }
}

KJ_TEST("kj::Weak<T> basic properties") {
  kj::Weak<Obj> nullWeak = nullptr;
  KJ_EXPECT(nullWeak == nullptr);
  KJ_IF_SOME(obj, nullWeak) {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on null Weak<T> to be empty", obj->name);
  } else {
    KJ_EXPECT(true);
  }

  kj::Pin<Obj> pin("a");

  kj::Weak<Obj> weak1 = pin.addWeak();
  KJ_EXPECT(weak1 == pin);
  KJ_EXPECT(pin == weak1);
  KJ_EXPECT(weak1.assertLive().name == "a"_kj);

  KJ_IF_SOME(obj, weak1.tryGet()) {
    static_assert(kj::isSameType<decltype(obj), Obj&>());
    KJ_EXPECT(obj.name == "a"_kj);
  } else {
    KJ_FAIL_EXPECT("expected Weak<T> to contain a pointer");
  }

  KJ_IF_SOME(obj, weak1) {
    static_assert(kj::isSameType<decltype(obj), kj::Ptr<Obj>&>());
    KJ_EXPECT(obj->name == "a"_kj);
    obj->name = kj::str("b");
  } else {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on Weak<T> to contain a pointer");
  }
  KJ_EXPECT(pin->name == "b"_kj);

  const auto& constWeak = weak1;
  KJ_IF_SOME(obj, constWeak) {
    static_assert(kj::isSameType<decltype(obj), kj::Ptr<const Obj>&>());
    KJ_EXPECT(obj->name == "b"_kj);
  } else {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on const Weak<T> to contain a pointer");
  }

  KJ_IF_SOME(obj, pin.addWeak()) {
    static_assert(kj::isSameType<decltype(obj), kj::Ptr<Obj>&>());
    KJ_EXPECT(obj->name == "b"_kj);
  } else {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on temporary Weak<T> to contain a pointer");
  }

  kj::Weak<Obj> weak2 = weak1;
  KJ_EXPECT(weak1 == weak2);
  KJ_EXPECT(weak2.assertLive().name == "b"_kj);

  weak2 = nullptr;
  KJ_EXPECT(weak2 == nullptr);
}

KJ_TEST("kj::Ptr<T> and kj::Weak<T> conversion") {
  kj::Pin<Obj> pin("a");

  kj::Ptr<Obj> ptr = pin;
  kj::Weak<Obj> weak = ptr.asWeak();
  KJ_EXPECT(weak == pin);
  KJ_EXPECT(weak.assertLive().name == "a"_kj);

  kj::Weak<Obj> weakFromPtr = ptr;
  KJ_EXPECT(weakFromPtr == pin);
  KJ_EXPECT(weakFromPtr.assertLive().name == "a"_kj);

  kj::Weak<Obj> weakFromTemp = pin.asPtr();
  KJ_EXPECT(weakFromTemp == pin);
  KJ_EXPECT(weakFromTemp.assertLive().name == "a"_kj);

  KJ_IF_SOME(strong, weak) {
    static_assert(kj::isSameType<decltype(strong), kj::Ptr<Obj>&>());
    KJ_EXPECT(strong == pin);
    KJ_EXPECT(strong->name == "a"_kj);

    kj::Weak<Obj> weak2 = strong.asWeak();
    KJ_EXPECT(weak2 == pin);
  } else {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on Weak<T> to upgrade");
  }

  auto strongFromRequire = KJ_REQUIRE_NONNULL(weak);
  static_assert(kj::isSameType<decltype(strongFromRequire), kj::Ptr<Obj>>());
  KJ_EXPECT(strongFromRequire == pin);
  KJ_EXPECT(strongFromRequire->name == "a"_kj);

  KJ_IF_SOME(strong, weak.upgrade()) {
    KJ_EXPECT(strong == pin);
    KJ_EXPECT(strong->name == "a"_kj);

    kj::Weak<Obj> weak2 = strong.asWeak();
    KJ_EXPECT(weak2 == pin);
  } else {
    KJ_FAIL_EXPECT("expected Weak<T> to upgrade");
  }

  const auto& constWeak = weak;
  KJ_IF_SOME(strong, constWeak) {
    static_assert(kj::isSameType<decltype(strong), kj::Ptr<const Obj>&>());
    KJ_EXPECT(strong->name == "a"_kj);
  } else {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on const Weak<T> to upgrade");
  }

  auto strongFromConstRequire = KJ_REQUIRE_NONNULL(constWeak);
  static_assert(kj::isSameType<decltype(strongFromConstRequire), kj::Ptr<const Obj>>());
  KJ_EXPECT(strongFromConstRequire->name == "a"_kj);

  KJ_IF_SOME(strong, constWeak.upgrade()) {
    static_assert(kj::isSameType<decltype(strong), kj::Ptr<const Obj>&>());
    KJ_EXPECT(strong->name == "a"_kj);
  } else {
    KJ_FAIL_EXPECT("expected const Weak<T> to upgrade");
  }
}

KJ_TEST("kj::Weak<T> expires when Pin<T> is destroyed") {
  kj::Maybe<kj::Weak<Obj>> maybeWeak;
  {
    kj::Pin<Obj> pin("a");
    maybeWeak = pin.addWeak();

    KJ_IF_SOME(weak, maybeWeak) {
      KJ_EXPECT(weak.assertLive().name == "a"_kj);
    } else {
      KJ_FAIL_EXPECT("expected Maybe<Weak<T>> to contain a pointer");
    }
  }

  KJ_IF_SOME(weak, maybeWeak) {
    KJ_EXPECT(weak.tryGet() == kj::none);
    KJ_EXPECT(weak.upgrade() == kj::none);
    KJ_IF_SOME(obj, weak) {
      KJ_FAIL_EXPECT("expected KJ_IF_SOME on expired Weak<T> to be empty", obj->name);
    } else {
      KJ_EXPECT(true);
    }
    KJ_EXPECT_THROW_MESSAGE("null Weak<> dereference", (void)weak.assertLive());
  } else {
    KJ_FAIL_EXPECT("expected Maybe<Weak<T>> to contain an expired pointer");
  }
}

KJ_TEST("kj::Pin<T> moved with active weak refs expires weak refs") {
  kj::Maybe<kj::Weak<Obj>> maybeWeak;
  {
    kj::Pin<Obj> pin("a");
    maybeWeak = pin.addWeak();

    kj::Pin<Obj> pin2(kj::mv(pin));
    KJ_EXPECT(pin2->name == "a"_kj);

    KJ_IF_SOME(weak, maybeWeak) {
      KJ_EXPECT(weak.tryGet() == kj::none);
      KJ_EXPECT(weak.upgrade() == kj::none);

      kj::Weak<Obj> newWeak = pin2.addWeak();
      KJ_EXPECT(newWeak == pin2);
      KJ_EXPECT(newWeak.assertLive().name == "a"_kj);
    } else {
      KJ_FAIL_EXPECT("expected Maybe<Weak<T>> to contain a pointer");
    }
  }

  KJ_IF_SOME(weak, maybeWeak) {
    KJ_EXPECT(weak.tryGet() == kj::none);
  } else {
    KJ_FAIL_EXPECT("expected Maybe<Weak<T>> to contain an expired pointer");
  }
}

KJ_TEST("Maybe<kj::Ptr<T>> niche optimization") {
  static_assert(sizeof(kj::Maybe<kj::Ptr<Obj>>) == sizeof(kj::Ptr<Obj>),
      "Maybe<Ptr<T>> should have no size overhead due to niche optimization");
  static_assert(alignof(kj::Maybe<kj::Ptr<Obj>>) == alignof(kj::Ptr<Obj>),
      "Maybe<Ptr<T>> should preserve Ptr<T>'s alignment");

  kj::Maybe<kj::Ptr<Obj>> empty;
  KJ_EXPECT(empty == kj::none);

  kj::Pin<Obj> pin("a");
  empty = pin.asPtr();

  KJ_IF_SOME(ptr, empty) {
    KJ_EXPECT(ptr == pin);
    KJ_EXPECT(ptr->name == "a"_kj);
  } else {
    KJ_FAIL_EXPECT("expected Maybe<Ptr<T>> to contain a pointer");
  }

  kj::Maybe<kj::Ptr<Obj>> copy = empty;
  KJ_IF_SOME(ptr, copy) {
    KJ_EXPECT(ptr == pin);
  } else {
    KJ_FAIL_EXPECT("expected copied Maybe<Ptr<T>> to contain a pointer");
  }

  KJ_IF_SOME(ptr, kj::mv(empty)) {
    KJ_EXPECT(ptr == pin);
  } else {
    KJ_FAIL_EXPECT("expected moved Maybe<Ptr<T>> to contain a pointer");
  }
  KJ_EXPECT(empty == kj::none);

  copy = kj::none;
  KJ_EXPECT(copy == kj::none);
}

KJ_TEST("Maybe<kj::Ptr<T>> converting constructor") {
  static_assert(kj::MaybeTraits<kj::Ptr<Obj>>::convertingConstructor,
      "Maybe<Ptr<T>> should opt into converting construction");
  static_assert(kj::canConvert<kj::Ptr<Obj2>, kj::Maybe<kj::Ptr<Obj>>>(),
      "Maybe<Ptr<Base>> should be implicitly constructible from Ptr<Derived>");
  static_assert(!kj::canConvert<kj::Ptr<Obj>, kj::Maybe<kj::Ptr<Obj2>>>(),
      "Maybe<Ptr<Derived>> should not be implicitly constructible from Ptr<Base>");

  kj::Pin<Obj2> pin("obj2", 42);

  kj::Maybe<kj::Ptr<Obj>> maybe = pin.asPtr();
  KJ_IF_SOME(ptr, maybe) {
    KJ_EXPECT(ptr == pin);
    KJ_EXPECT(ptr->name == "obj2"_kj);
  } else {
    KJ_FAIL_EXPECT("expected Maybe<Ptr<Base>> to be constructed from Ptr<Derived>");
  }

  auto makeMaybe = [](kj::Pin<Obj2>& pin) -> kj::Maybe<kj::Ptr<Obj>> {
    return pin.asPtr();
  };
  KJ_IF_SOME(ptr, makeMaybe(pin)) {
    KJ_EXPECT(ptr == pin);
    KJ_EXPECT(ptr->name == "obj2"_kj);
  } else {
    KJ_FAIL_EXPECT("expected Ptr<Derived> return to convert to Maybe<Ptr<Base>>");
  }

  kj::Maybe<kj::Ptr<Obj>> assigned;
  assigned = pin.asPtr();
  KJ_IF_SOME(ptr, assigned) {
    KJ_EXPECT(ptr == pin);
    KJ_EXPECT(ptr->name == "obj2"_kj);
  } else {
    KJ_FAIL_EXPECT("expected Maybe<Ptr<Base>> to be assigned from Ptr<Derived>");
  }
}

#if KJ_ASSERT_PTR_COUNTERS
KJ_TEST("kj::Pin<T> destroyed with active ptrs crashed") {
  PtrHolder* holder = nullptr;
  
  KJ_EXPECT_SIGNAL(SIGABRT, {
    kj::Pin<Obj> obj("b");
    // create a pointer and leak it
    holder = new PtrHolder { obj };
    // destroying a pin when exiting scope crashes
  });
}

KJ_TEST("kj::Pin<T> moved with active ptrs crashes") {
  KJ_EXPECT_SIGNAL(SIGABRT, {
    kj::Pin<Obj> obj("b");
    auto ptr = obj.asPtr();
    // moving a pin with active reference crashes
    kj::Pin<Obj> obj2(kj::mv(obj));
  });
}
#else
KJ_TEST("kj::Ptr<T> expires when Pin<T> is destroyed in opt mode") {
  kj::Maybe<kj::Ptr<Obj>> maybePtr;

  {
    kj::Pin<Obj> obj("b");
    maybePtr = obj.asPtr();

    KJ_IF_SOME(ptr, maybePtr) {
      KJ_EXPECT(ptr == obj);
      KJ_EXPECT(ptr->name == "b"_kj);
    } else {
      KJ_FAIL_EXPECT("expected Maybe<Ptr<T>> to contain a pointer");
    }
  }

  KJ_IF_SOME(ptr, maybePtr) {
    KJ_EXPECT(ptr == nullptr);
    KJ_EXPECT_THROW_MESSAGE("null Ptr<> dereference", (void)ptr->name);
    KJ_EXPECT_THROW_MESSAGE("null Ptr<> dereference", (void)ptr.asRef());

    kj::Weak<Obj> weak = ptr.asWeak();
    KJ_EXPECT(weak == nullptr);
    KJ_EXPECT(weak.upgrade() == kj::none);
  } else {
    KJ_FAIL_EXPECT("expected Maybe<Ptr<T>> to contain an expired pointer");
  }
}

KJ_TEST("kj::Ptr<T> expires when Pin<T> is moved in opt mode") {
  kj::Maybe<kj::Ptr<Obj>> maybePtr;

  {
    kj::Pin<Obj> obj("b");
    maybePtr = obj.asPtr();
    kj::Pin<Obj> obj2(kj::mv(obj));

    KJ_IF_SOME(ptr, maybePtr) {
      KJ_EXPECT(ptr == nullptr);
      KJ_EXPECT(!(ptr == obj2));
      KJ_EXPECT_THROW_MESSAGE("null Ptr<> dereference", (void)ptr->name);

      kj::Weak<Obj> weak = ptr.asWeak();
      KJ_EXPECT(weak == nullptr);
      KJ_EXPECT(weak.upgrade() == kj::none);
    } else {
      KJ_FAIL_EXPECT("expected Maybe<Ptr<T>> to contain an expired pointer");
    }

    auto newPtr = obj2.asPtr();
    KJ_EXPECT(newPtr == obj2);
    KJ_EXPECT(newPtr->name == "b"_kj);
  }
}
#endif

} // namespace 

}  // namespace kj
