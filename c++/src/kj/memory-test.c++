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

#include "kj/common.h"
#include "kj/string.h"
#include "kj/test.h"
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
  KJ_EXPECT(m != kj::none);
  Maybe<int&> mRef = m;
  KJ_EXPECT(KJ_ASSERT_NONNULL(mRef) == 123);
  KJ_EXPECT(&KJ_ASSERT_NONNULL(mRef) == KJ_ASSERT_NONNULL(m).get());
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

  // pointers can be converted to the base type too
  kj::Ptr<Obj> ptr3 = kj::mv(ptr1);
  KJ_EXPECT(ptr3->name == "obj2"_kj);
  KJ_EXPECT(ptr3 == pin);
}

#ifdef KJ_ASSERT_PTR_COUNTERS  
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
#endif  

} // namespace 

}  // namespace kj
