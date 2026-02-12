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

#include "common.h"
#include "test.h"
#include <stdexcept>

namespace kj {
namespace {

struct ImplicitToInt {
  int i;

  operator int() const {
    return i;
  }
};

struct Immovable {
  Immovable() = default;
  KJ_DISALLOW_COPY_AND_MOVE(Immovable);
};

struct CopyOrMove {
  // Type that detects the difference between copy and move.
  CopyOrMove(int i): i(i) {}
  CopyOrMove(CopyOrMove&& other): i(other.i) { other.i = -1; }
  CopyOrMove(const CopyOrMove&) = default;

  int i;
};

// =======================================================================================

// Test types for converting constructor tests

struct Base {
  int value;
  explicit Base(int v): value(v) {}
  virtual ~Base() = default;
};

struct Derived: Base {
  explicit Derived(int v): Base(v) {}
};

// Niche optimization test types

struct MoveOnlyNiche {
  // A move-only type where value == 0 represents the "none" niche state.
  int value;

  explicit MoveOnlyNiche(int v): value(v) {}
  MoveOnlyNiche(MoveOnlyNiche&& other): value(other.value) { other.value = 0; }
  MoveOnlyNiche& operator=(MoveOnlyNiche&& other) {
    value = other.value;
    other.value = 0;
    return *this;
  }
  KJ_DISALLOW_COPY(MoveOnlyNiche);

  friend struct ::kj::MaybeTraits<MoveOnlyNiche>;
};

struct NonMoveableNiche {
  // A non-moveable type where value == 0 represents the "none" niche state.
  int value;

  explicit NonMoveableNiche(int v): value(v) {}
  KJ_DISALLOW_COPY_AND_MOVE(NonMoveableNiche);

  friend struct ::kj::MaybeTraits<NonMoveableNiche>;
};

struct NicheInt {
  // A niche-optimized int wrapper. Uses -1 as none (so 0 is a valid value).
  // Has implicit copy/move that doesn't change the source (just copies the int).
  int value;

  NicheInt(int v): value(v) {}
  operator int() const { return value; }

  friend struct ::kj::MaybeTraits<NicheInt>;
};

struct NonNicheInt {
  // A non-niche-optimized int wrapper.
  // No MaybeTraits, so Maybe<NonNicheInt> uses the regular isSet-based NullableValue.
  int value;

  NonNicheInt(int v): value(v) {}
  operator int() const { return value; }
};

struct NoneThrowingNiche {
  // A niche-optimized type that throws if move/copy constructed from its none state.
  // This verifies that NullableValue never tries to construct from a none value.
  int value;

  explicit NoneThrowingNiche(int v): value(v) {}

  NoneThrowingNiche(NoneThrowingNiche&& other): value(other.value) {
    if (other.value == 0) {
      throw std::logic_error("moved from none!");
    }
    other.value = -1;  // moved-from state, NOT none
  }

  NoneThrowingNiche(const NoneThrowingNiche& other): value(other.value) {
    if (other.value == 0) {
      throw std::logic_error("copied from none!");
    }
  }

  NoneThrowingNiche& operator=(NoneThrowingNiche&& other) {
    if (other.value == 0) {
      throw std::logic_error("move-assigned from none!");
    }
    value = other.value;
    other.value = -1;  // moved-from state, NOT none
    return *this;
  }

  NoneThrowingNiche& operator=(const NoneThrowingNiche& other) {
    if (other.value == 0) {
      throw std::logic_error("copy-assigned from none!");
    }
    value = other.value;
    return *this;
  }

  friend struct ::kj::MaybeTraits<NoneThrowingNiche>;
};

struct NoneDestructorCounter {
  // A niche-optimized type that counts destructor calls and asserts its destructor is never
  // called with the none value. This verifies that Maybe never destroys none values.
  int value;

  static int nonNoneDestroyCount;
  static int noneDestroyCount;  // Should always remain 0

  explicit NoneDestructorCounter(int v): value(v) {}

  NoneDestructorCounter(NoneDestructorCounter&& other): value(other.value) {
    other.value = -1;  // moved-from state, NOT none (which is 0)
  }

  NoneDestructorCounter& operator=(NoneDestructorCounter&& other) {
    value = other.value;
    other.value = -1;  // moved-from state, NOT none
    return *this;
  }

  ~NoneDestructorCounter() {
    if (value == 0) {
      ++noneDestroyCount;  // This should never happen!
    } else {
      ++nonNoneDestroyCount;
    }
  }

  KJ_DISALLOW_COPY(NoneDestructorCounter);

  friend struct ::kj::MaybeTraits<NoneDestructorCounter>;
};

int NoneDestructorCounter::nonNoneDestroyCount = 0;
int NoneDestructorCounter::noneDestroyCount = 0;

}  // namespace

// MaybeTraits specializations for test types
template <>
struct MaybeTraits<MoveOnlyNiche> {
  // Niche optimization: value == 0 is the "none" state
  static void initNone(MoveOnlyNiche* ptr) noexcept { kj::ctor(*ptr, 0); }
  static bool isNone(const MoveOnlyNiche& m) noexcept { return m.value == 0; }
};

template <>
struct MaybeTraits<NonMoveableNiche> {
  // Niche optimization: value == 0 is the "none" state
  static void initNone(NonMoveableNiche* ptr) noexcept { kj::ctor(*ptr, 0); }
  static bool isNone(const NonMoveableNiche& m) noexcept { return m.value == 0; }
};

template <>
struct MaybeTraits<NicheInt> {
  // Niche optimization: value == -1 is the "none" state
  static void initNone(NicheInt* ptr) noexcept { kj::ctor(*ptr, -1); }
  static bool isNone(const NicheInt& m) noexcept { return m.value == -1; }
};

template <>
struct MaybeTraits<NoneThrowingNiche> {
  // Niche optimization: value == 0 is the "none" state
  static void initNone(NoneThrowingNiche* ptr) noexcept { kj::ctor(*ptr, 0); }
  static bool isNone(const NoneThrowingNiche& m) noexcept { return m.value == 0; }
};

template <>
struct MaybeTraits<NoneDestructorCounter> {
  // Niche optimization: value == 0 is the "none" state
  static void initNone(NoneDestructorCounter* ptr) noexcept { kj::ctor(*ptr, 0); }
  static bool isNone(const NoneDestructorCounter& m) noexcept { return m.value == 0; }
};

namespace {

// =======================================================================================
// Test wrapper for dereferencingConversion functionality (standalone, doesn't depend on memory.h)

// A simple smart pointer type for testing Maybe<T> -> Maybe<U&> conversion
template <typename T>
class TestPtr {
public:
  TestPtr(): ptr(nullptr) {}
  explicit TestPtr(T* p): ptr(p) {}
  ~TestPtr() { delete ptr; }

  TestPtr(TestPtr&& other): ptr(other.ptr) { other.ptr = nullptr; }
  TestPtr& operator=(TestPtr&& other) {
    if (this != &other) {
      delete ptr;
      ptr = other.ptr;
      other.ptr = nullptr;
    }
    return *this;
  }

  // Converting constructor: TestPtr<U> -> TestPtr<T> when U* converts to T*
  // This mimics Own<T>'s behavior of allowing Own<Derived> -> Own<Base>.
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  TestPtr(TestPtr<U>&& other): ptr(other.ptr) { other.ptr = nullptr; }

  KJ_DISALLOW_COPY(TestPtr);

  T* get() const { return ptr; }
  T& operator*() const { return *ptr; }
  T* operator->() const { return ptr; }

private:
  T* ptr;

  template <typename U>
  friend class TestPtr;
  friend struct ::kj::MaybeTraits<TestPtr<T>>;
};

template <typename T>
TestPtr<T> makeTestPtr(T* p) { return TestPtr<T>(p); }

}  // namespace (anonymous)

// MaybeTraits for TestPtr - enables niche optimization, converting constructor, and deref conversion
template <typename T>
struct MaybeTraits<TestPtr<T>> {
  // Niche optimization: nullptr is the "none" state
  static void initNone(TestPtr<T>* ptr) noexcept { kj::ctor(*ptr); }
  static bool isNone(const TestPtr<T>& p) noexcept { return p.get() == nullptr; }

  // Allow implicit conversions (use T's implicit conversions)
  static constexpr bool convertingConstructor = true;

  // Reference conversion: Maybe<TestPtr<T>> -> Maybe<T&> via dereference
  static constexpr bool dereferencingConversion = true;
};

namespace {

KJ_TEST("Maybe") {
  {
    Maybe<int> m = 123;
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(123 == v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(123 == v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_EXPECT(123 == m.orDefault(456));
    bool ranLazy = false;
    KJ_EXPECT(123 == m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    KJ_EXPECT(!ranLazy);

    KJ_IF_SOME(v, m) {
      int notUsedForRef = 5;
      const int& ref = m.orDefault([&]() -> int& { return notUsedForRef; });

      KJ_EXPECT(ref == v);
      KJ_EXPECT(&ref == &v);

      const int& ref2 = m.orDefault([notUsed = 5]() -> int { return notUsed; });
      KJ_EXPECT(&ref != &ref2);
      KJ_EXPECT(ref2 == 123);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
  }

  {
    Maybe<Own<CopyOrMove>> m = kj::heap<CopyOrMove>(123);
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(123 == v->i);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(123 == v->i);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    // We have moved the kj::Own away, so this should give us the default and leave the Maybe empty.
    KJ_EXPECT(456 == m.orDefault(heap<CopyOrMove>(456))->i);
    KJ_EXPECT(m == kj::none);

    bool ranLazy = false;
    KJ_EXPECT(123 == mv(m).orDefault([&] {
      ranLazy = true;
      return heap<CopyOrMove>(123);
    })->i);
    KJ_EXPECT(ranLazy);
    KJ_EXPECT(m == kj::none);

    m = heap<CopyOrMove>(123);
    KJ_EXPECT(m != kj::none);
    ranLazy = false;
    KJ_EXPECT(123 == mv(m).orDefault([&] {
      ranLazy = true;
      return heap<CopyOrMove>(456);
    })->i);
    KJ_EXPECT(!ranLazy);
    KJ_EXPECT(m == kj::none);
  }

  {
    Maybe<int> empty;
    int defaultValue = 5;
    auto& ref1 = empty.orDefault([&defaultValue]() -> int& {
      return defaultValue;
    });
    KJ_EXPECT(&ref1 == &defaultValue);

    auto ref2 = empty.orDefault([&]() -> int { return defaultValue; });
    KJ_EXPECT(&ref2 != &defaultValue);
  }

  {
    Maybe<int> m = 0;
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(0 == v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(0 == v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_EXPECT(0 == m.orDefault(456));
    bool ranLazy = false;
    KJ_EXPECT(0 == m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    KJ_EXPECT(!ranLazy);
  }

  {
    Maybe<int> m = kj::none;
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(!(m != kj::none));
    KJ_IF_SOME(v, m) {
      KJ_FAIL_EXPECT("unexpected");
      KJ_EXPECT(0 == v);  // avoid unused warning
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_FAIL_EXPECT("unexpected");
      KJ_EXPECT(0 == v);  // avoid unused warning
    }
    KJ_EXPECT(456 == m.orDefault(456));
    bool ranLazy = false;
    KJ_EXPECT(456 == m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    KJ_EXPECT(ranLazy);
  }

  int i = 234;
  {
    Maybe<int&> m = i;
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(&i == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(&i == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_EXPECT(234 == m.orDefault(456));
  }

  {
    Maybe<int&> m = kj::none;
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(!(m != kj::none));
    KJ_IF_SOME(v, m) {
      KJ_FAIL_EXPECT("unexpected");
      KJ_EXPECT(0 == v);  // avoid unused warning
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_FAIL_EXPECT("unexpected");
      KJ_EXPECT(0 == v);  // avoid unused warning
    }
    KJ_EXPECT(456 == m.orDefault(456));
  }

  {
    Maybe<int&> m = &i;
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(&i == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(&i == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_EXPECT(234 == m.orDefault(456));
  }

  {
    const Maybe<int&> m2 = &i;
    Maybe<const int&> m = m2;
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(&i == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(&i == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_EXPECT(234 == m.orDefault(456));
  }

  {
    Maybe<int&> m = implicitCast<int*>(nullptr);
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(!(m != kj::none));
    KJ_IF_SOME(v, m) {
      KJ_FAIL_EXPECT("unexpected");
      KJ_EXPECT(0 == v);  // avoid unused warning
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_FAIL_EXPECT("unexpected");
      KJ_EXPECT(0 == v);  // avoid unused warning
    }
    KJ_EXPECT(456 == m.orDefault(456));
  }

  {
    Maybe<int> mi = i;
    Maybe<int&> m = mi;
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(&KJ_ASSERT_NONNULL(mi) == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(&KJ_ASSERT_NONNULL(mi) == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_EXPECT(234 == m.orDefault(456));
  }

  {
    Maybe<int> mi = kj::none;
    Maybe<int&> m = mi;
    KJ_EXPECT(m == kj::none);
    KJ_IF_SOME(v, m) {
      KJ_FAIL_EXPECT(v);
    }
  }

  {
    const Maybe<int> mi = i;
    Maybe<const int&> m = mi;
    KJ_EXPECT(!(m == kj::none));
    KJ_EXPECT(m != kj::none);
    KJ_IF_SOME(v, m) {
      KJ_EXPECT(&KJ_ASSERT_NONNULL(mi) == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, mv(m)) {
      KJ_EXPECT(&KJ_ASSERT_NONNULL(mi) == &v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_EXPECT(234 == m.orDefault(456));
  }

  {
    const Maybe<int> mi = kj::none;
    Maybe<const int&> m = mi;
    KJ_EXPECT(m == kj::none);
    KJ_IF_SOME(v, m) {
      KJ_FAIL_EXPECT(v);
    }
  }

  {
    // Verify orDefault() works with move-only types.
    Maybe<kj::String> m = kj::none;
    kj::String s = kj::mv(m).orDefault(kj::str("foo"));
    KJ_EXPECT("foo" == s);
    KJ_EXPECT("foo" == kj::mv(m).orDefault([] {
      return kj::str("foo");
    }));
  }

  {
    // Test a case where an implicit conversion didn't used to happen correctly.
    Maybe<ImplicitToInt> m(ImplicitToInt { 123 });
    Maybe<uint> m2(m);
    Maybe<uint> m3(kj::mv(m));
    KJ_IF_SOME(v, m2) {
      KJ_EXPECT(123 == v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
    KJ_IF_SOME(v, m3) {
      KJ_EXPECT(123 == v);
    } else {
      KJ_FAIL_EXPECT("unexpected");
    }
  }

  {
    // Test usage of immovable types.
    Maybe<Immovable> m;
    KJ_EXPECT(m == kj::none);
    m.emplace();
    KJ_EXPECT(m != kj::none);
    m = kj::none;
    KJ_EXPECT(m == kj::none);
  }

  {
    // Test that initializing Maybe<T> from Maybe<T&>&& does a copy, not a move.
    CopyOrMove x(123);
    Maybe<CopyOrMove&> m(x);
    Maybe<CopyOrMove> m2 = kj::mv(m);
    KJ_EXPECT(m == kj::none);  // m is moved out of and cleared
    KJ_EXPECT(x.i == 123);  // but what m *referenced* was not moved out of
    KJ_EXPECT(KJ_ASSERT_NONNULL(m2).i == 123);  // m2 is a copy of what m referenced
  }

  {
    // Test that a moved-out-of Maybe<T> is left empty after move constructor.
    Maybe<int> m = 123;
    KJ_EXPECT(m != kj::none);

    Maybe<int> n(kj::mv(m));
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(n != kj::none);
  }

  {
    // Test that a moved-out-of Maybe<T> is left empty after move constructor.
    Maybe<int> m = 123;
    KJ_EXPECT(m != kj::none);

    Maybe<int> n = kj::mv(m);
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(n != kj::none);
  }

  {
    // Test that a moved-out-of Maybe<T&> is left empty when moved to a Maybe<T>.
    int x = 123;
    Maybe<int&> m = x;
    KJ_EXPECT(m != kj::none);

    Maybe<int> n(kj::mv(m));
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(n != kj::none);
  }

  {
    // Test that a moved-out-of Maybe<T&> is left empty when moved to another Maybe<T&>.
    int x = 123;
    Maybe<int&> m = x;
    KJ_EXPECT(m != kj::none);

    Maybe<int&> n(kj::mv(m));
    KJ_EXPECT(m == kj::none);
    KJ_EXPECT(n != kj::none);
  }

  {
    Maybe<int> m1 = 123;
    Maybe<int> m2 = 123;
    Maybe<int> m3 = 456;
    Maybe<int> m4 = kj::none;
    Maybe<int> m5 = kj::none;

    KJ_EXPECT(m1 == m2);
    KJ_EXPECT(m1 != m3);
    KJ_EXPECT(m1 != m4);
    KJ_EXPECT(m4 == m5);
    KJ_EXPECT(m4 != m1);
  }

  {
    // type deduction in various circumstances
    struct IntWrapper {
      IntWrapper(int i): i(i) {}
      int i;

      static int twice(Maybe<IntWrapper> i) {
        return i.orDefault(0).i * 2;
      }
    };

    // You can't write twice(5), you need to specify some of the types
    KJ_EXPECT(10 == IntWrapper::twice(Maybe<IntWrapper>(5)));
    KJ_EXPECT(10 == IntWrapper::twice(IntWrapper(5)));

    // kj::some solves this problem elegantly
    KJ_EXPECT(10 == IntWrapper::twice(kj::some(5)));
  }
}

KJ_TEST("Maybe constness") {
  int i;

  Maybe<int&> mi = i;
  const Maybe<int&> cmi = mi;
//  const Maybe<int&> cmi2 = cmi;    // shouldn't compile!  Transitive const violation.

  KJ_IF_SOME(i2, cmi) {
    KJ_EXPECT(&i == &i2);
  } else {
    KJ_FAIL_EXPECT("unexpected");
  }

  Maybe<const int&> mci = mi;
  const Maybe<const int&> cmci = mci;
  const Maybe<const int&> cmci2 = cmci;

  KJ_IF_SOME(i2, cmci2) {
    KJ_EXPECT(&i == &i2);
  } else {
    KJ_FAIL_EXPECT("unexpected");
  }
}

#if __GNUC__
KJ_TEST("Maybe unwrap or return") {
  {
    auto func = [](Maybe<int> i) -> int {
      int& j = KJ_UNWRAP_OR_RETURN(i, -1);
      KJ_EXPECT(&j == &KJ_ASSERT_NONNULL(i));
      return j + 2;
    };

    KJ_EXPECT(func(123) == 125);
    KJ_EXPECT(func(kj::none) == -1);
  }

  {
    auto func = [&](Maybe<String> maybe) -> int {
      String str = KJ_UNWRAP_OR_RETURN(kj::mv(maybe), -1);
      return str.parseAs<int>();
    };

    KJ_EXPECT(func(kj::str("123")) == 123);
    KJ_EXPECT(func(kj::none) == -1);
  }

  // Test void return.
  {
    int val = 0;
    auto func = [&](Maybe<int> i) {
      val = KJ_UNWRAP_OR_RETURN(i);
    };

    func(123);
    KJ_EXPECT(val == 123);
    val = 321;
    func(kj::none);
    KJ_EXPECT(val == 321);
  }

  // Test KJ_UNWRAP_OR
  {
    bool wasNull = false;
    auto func = [&](Maybe<int> i) -> int {
      int& j = KJ_UNWRAP_OR(i, {
        wasNull = true;
        return -1;
      });
      KJ_EXPECT(&j == &KJ_ASSERT_NONNULL(i));
      return j + 2;
    };

    KJ_EXPECT(func(123) == 125);
    KJ_EXPECT(!wasNull);
    KJ_EXPECT(func(kj::none) == -1);
    KJ_EXPECT(wasNull);
  }

  {
    bool wasNull = false;
    auto func = [&](Maybe<String> maybe) -> int {
      String str = KJ_UNWRAP_OR(kj::mv(maybe), {
        wasNull = true;
        return -1;
      });
      return str.parseAs<int>();
    };

    KJ_EXPECT(func(kj::str("123")) == 123);
    KJ_EXPECT(!wasNull);
    KJ_EXPECT(func(kj::none) == -1);
    KJ_EXPECT(wasNull);
  }

  // Test void return.
  {
    int val = 0;
    auto func = [&](Maybe<int> i) {
      val = KJ_UNWRAP_OR(i, {
        return;
      });
    };

    func(123);
    KJ_EXPECT(val == 123);
    val = 321;
    func(kj::none);
    KJ_EXPECT(val == 321);
  }

}
#endif

// =======================================================================================
// Maybe<T> converting constructor tests
//
// The convertingConstructor MaybeTraits option exists to solve the "two user-defined conversions"
// problem. In C++, an implicit conversion sequence can only have ONE user-defined conversion.
// Without convertingConstructor=true:
//   Maybe<TestPtr<Base>> m = testPtrDerived;  // FAILS: TestPtr<Derived>->TestPtr<Base> is UDC #1,
//                                              //        then TestPtr<Base>->Maybe is UDC #2
// With convertingConstructor=true:
//   Maybe<TestPtr<Base>> m = testPtrDerived;  // WORKS: TestPtr<Derived>->Maybe<TestPtr<Base>> is
//                                              //        a single conversion via Maybe's converting ctor

KJ_TEST("Maybe convertingConstructor enables implicit two-step conversion") {
  // This test verifies that MaybeTraits::convertingConstructor=true allows implicit conversion
  // from TestPtr<Derived> to Maybe<TestPtr<Base>>. Without convertingConstructor, this would
  // require two user-defined conversions and fail to compile.
  //
  // TestPtr has convertingConstructor=true in its MaybeTraits, mimicking Own<T>'s behavior.

  // Direct construction: TestPtr<Derived> -> Maybe<TestPtr<Base>>
  {
    TestPtr<Derived> derived(new Derived(42));
    Maybe<TestPtr<Base>> m = kj::mv(derived);  // Implicit conversion!
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 42);
  }

  // Return from function - the primary use case for convertingConstructor
  {
    auto makeWidget = []() -> Maybe<TestPtr<Base>> {
      return TestPtr<Derived>(new Derived(123));  // No braces needed!
    };
    Maybe<TestPtr<Base>> m = makeWidget();
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 123);
  }

  // Assignment from TestPtr<Derived> to Maybe<TestPtr<Base>>
  {
    Maybe<TestPtr<Base>> m;
    m = TestPtr<Derived>(new Derived(99));
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 99);
  }
}

// =======================================================================================
// Maybe<T> -> Maybe<U&> reference conversion via MaybeTraits::dereferencingConversion
//
// This test uses TestPtr (defined above) which is a standalone smart pointer type
// that doesn't depend on memory.h. This tests the dereferencingConversion mechanism in isolation.

// Types for testing reference conversion with inheritance
struct RefBase {
  int value;
  explicit RefBase(int v): value(v) {}
  virtual ~RefBase() = default;
};

struct RefDerived: RefBase {
  explicit RefDerived(int v): RefBase(v) {}
};

KJ_TEST("Maybe<TestPtr<T>> implicit conversion to Maybe<U&> via dereferencingConversion") {
  // Maybe<TestPtr<T>> can implicitly convert to Maybe<U&> when MaybeTraits::dereferencingConversion
  // is true. The conversion uses operator*() to dereference the smart pointer.

  // Lvalue conversion
  {
    Maybe<TestPtr<RefDerived>> m = makeTestPtr(new RefDerived(42));
    Maybe<RefDerived&> ref = m;
    KJ_EXPECT(ref != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(ref).value == 42);

    // Modifying through the reference affects the pointed-to value
    KJ_ASSERT_NONNULL(ref).value = 100;
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 100);
  }

  // Conversion to base type reference
  {
    Maybe<TestPtr<RefDerived>> m = makeTestPtr(new RefDerived(42));
    Maybe<RefBase&> baseRef = m;  // Derived* converts to Base*
    KJ_EXPECT(baseRef != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(baseRef).value == 42);
  }

  // Empty conversion
  {
    Maybe<TestPtr<RefDerived>> empty;
    Maybe<RefDerived&> ref = empty;
    KJ_EXPECT(ref == kj::none);
  }

  // Const conversion
  {
    const Maybe<TestPtr<RefDerived>> m = makeTestPtr(new RefDerived(42));
    Maybe<const RefDerived&> ref = m;
    KJ_EXPECT(KJ_ASSERT_NONNULL(ref).value == 42);
  }

  // Function parameter passing (common use case)
  {
    auto processValue = [](Maybe<RefBase&> ref) -> bool {
      KJ_IF_SOME(v, ref) {
        v.value *= 2;
        return true;
      }
      return false;
    };

    Maybe<TestPtr<RefDerived>> m = makeTestPtr(new RefDerived(21));
    KJ_EXPECT(processValue(m));
    KJ_EXPECT(KJ_ASSERT_NONNULL(m)->value == 42);

    Maybe<TestPtr<RefDerived>> empty;
    KJ_EXPECT(!processValue(empty));
  }
}

// =======================================================================================
// Niche optimization tests

KJ_TEST("Maybe<MoveOnlyNiche> niche optimization") {
  // Verify that Maybe<MoveOnlyNiche> uses niche optimization (no size overhead).
  static_assert(sizeof(Maybe<MoveOnlyNiche>) == sizeof(MoveOnlyNiche),
      "Maybe<MoveOnlyNiche> should have no size overhead due to niche optimization");

  // Test empty state
  Maybe<MoveOnlyNiche> empty;
  KJ_EXPECT(empty == kj::none);

  // Test construction with value
  Maybe<MoveOnlyNiche> a = MoveOnlyNiche(42);
  KJ_EXPECT(a != kj::none);

  KJ_IF_SOME(v, a) {
    KJ_EXPECT(v.value == 42);
  } else {
    KJ_FAIL_EXPECT("should have value");
  }

  // Test move semantics
  Maybe<MoveOnlyNiche> b = kj::mv(a);
  KJ_EXPECT(a == kj::none);  // moved-from should be none
  KJ_EXPECT(b != kj::none);

  KJ_IF_SOME(v, b) {
    KJ_EXPECT(v.value == 42);
  } else {
    KJ_FAIL_EXPECT("should have value");
  }

  // Test assignment to none
  b = kj::none;
  KJ_EXPECT(b == kj::none);

  // Test emplace
  b.emplace(123);
  KJ_EXPECT(b != kj::none);
  KJ_IF_SOME(v, b) {
    KJ_EXPECT(v.value == 123);
  } else {
    KJ_FAIL_EXPECT("should have value");
  }
}

KJ_TEST("Maybe<NonMoveableNiche> niche optimization") {
  // Verify that Maybe<NonMoveableNiche> uses niche optimization (no size overhead).
  static_assert(sizeof(Maybe<NonMoveableNiche>) == sizeof(NonMoveableNiche),
      "Maybe<NonMoveableNiche> should have no size overhead due to niche optimization");

  // NOTE: Ideally, Maybe<NonMoveableNiche> would itself be non-moveable, but the current
  // niche-optimized Maybe implementation doesn't conditionally delete its move/copy constructors.
  // This is a known limitation. The type traits report it as moveable/copyable even though
  // attempting to actually move/copy would fail to compile.

  // Test empty state
  Maybe<NonMoveableNiche> empty;
  KJ_EXPECT(empty == kj::none);

  // Test emplace (the only way to set a value for non-moveable types)
  Maybe<NonMoveableNiche> a;
  a.emplace(42);
  KJ_EXPECT(a != kj::none);

  KJ_IF_SOME(v, a) {
    KJ_EXPECT(v.value == 42);
  } else {
    KJ_FAIL_EXPECT("should have value");
  }

  // Test assignment to none
  a = kj::none;
  KJ_EXPECT(a == kj::none);

  // Test emplace again
  a.emplace(123);
  KJ_EXPECT(a != kj::none);
  KJ_IF_SOME(v, a) {
    KJ_EXPECT(v.value == 123);
  } else {
    KJ_FAIL_EXPECT("should have value");
  }
}

KJ_TEST("Maybe niche-optimized KJ_IF_SOME(v, kj::mv(m)) does not force source to none") {
  // Test that KJ_IF_SOME with kj::mv() does NOT force the source Maybe to none.
  // This is because KJ_IF_SOME extracts NullableValue&& via readMaybe(), which does NOT
  // go through Maybe's move constructor/assignment. The source remains in whatever state
  // T's move constructor left it in.
  //
  // NicheInt has implicit copy/move that just copies the int value without modifying the source.
  static_assert(sizeof(Maybe<NicheInt>) == sizeof(NicheInt), "Should be niche-optimized");

  Maybe<NicheInt> m = NicheInt(42);
  KJ_EXPECT(m != kj::none);

  KJ_IF_SOME(v, kj::mv(m)) {
    KJ_EXPECT(v.value == 42);
  } else {
    KJ_FAIL_EXPECT("should have value");
  }

  // m should NOT be none - NicheInt's move constructor copies the value,
  // it doesn't set the source to -1 (the none state).
  KJ_EXPECT(m != kj::none);
  KJ_IF_SOME(v, m) {
    KJ_EXPECT(v.value == 42);  // Still has the original value
  } else {
    KJ_FAIL_EXPECT("should still have value after KJ_IF_SOME(v, kj::mv(m))");
  }

  // Contrast with Maybe-to-Maybe move, which ALWAYS sets source to none.
  // This is true regardless of whether T is niche-optimized or not.
  Maybe<NicheInt> m2 = NicheInt(100);
  Maybe<NicheInt> m3 = kj::mv(m2);
  KJ_EXPECT(m3 != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(m3).value == 100);
  // m2 IS none after Maybe-to-Maybe move - Maybe always sets source to none.
  KJ_EXPECT(m2 == kj::none);
}

KJ_TEST("Maybe conversion between niche-optimized and non-niche-optimized types") {
  // Test converting between Maybe<NicheInt> and Maybe<NonNicheInt>.
  // NicheInt is niche-optimized (uses -1 as none), NonNicheInt is not.

  static_assert(sizeof(Maybe<NicheInt>) == sizeof(NicheInt),
      "NicheInt should be niche-optimized");
  static_assert(sizeof(Maybe<NonNicheInt>) > sizeof(NonNicheInt),
      "NonNicheInt should NOT be niche-optimized");

  // Test niche -> non-niche conversion (move)
  {
    Maybe<NicheInt> niche = NicheInt(42);
    Maybe<NonNicheInt> nonNiche = kj::mv(niche);
    KJ_EXPECT(nonNiche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(nonNiche).value == 42);
  }

  // Test niche -> non-niche conversion (copy)
  {
    Maybe<NicheInt> niche = NicheInt(42);
    Maybe<NonNicheInt> nonNiche = niche;
    KJ_EXPECT(nonNiche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(nonNiche).value == 42);
    // Original should still have value
    KJ_EXPECT(niche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(niche).value == 42);
  }

  // Test niche -> non-niche conversion from none
  {
    Maybe<NicheInt> niche;
    KJ_EXPECT(niche == kj::none);
    Maybe<NonNicheInt> nonNiche = kj::mv(niche);
    KJ_EXPECT(nonNiche == kj::none);
  }

  // Test non-niche -> niche conversion (move)
  {
    Maybe<NonNicheInt> nonNiche = NonNicheInt(42);
    Maybe<NicheInt> niche = kj::mv(nonNiche);
    KJ_EXPECT(niche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(niche).value == 42);
  }

  // Test non-niche -> niche conversion (copy)
  {
    Maybe<NonNicheInt> nonNiche = NonNicheInt(42);
    Maybe<NicheInt> niche = nonNiche;
    KJ_EXPECT(niche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(niche).value == 42);
    // Original should still have value
    KJ_EXPECT(nonNiche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(nonNiche).value == 42);
  }

  // Test non-niche -> niche conversion from none
  {
    Maybe<NonNicheInt> nonNiche;
    KJ_EXPECT(nonNiche == kj::none);
    Maybe<NicheInt> niche = kj::mv(nonNiche);
    KJ_EXPECT(niche == kj::none);
  }

  // Test niche -> non-niche assignment (move)
  {
    Maybe<NicheInt> niche = NicheInt(42);
    Maybe<NonNicheInt> nonNiche;
    nonNiche = kj::mv(niche);
    KJ_EXPECT(nonNiche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(nonNiche).value == 42);
  }

  // Test niche -> non-niche assignment (copy)
  {
    Maybe<NicheInt> niche = NicheInt(42);
    Maybe<NonNicheInt> nonNiche;
    nonNiche = niche;
    KJ_EXPECT(nonNiche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(nonNiche).value == 42);
  }

  // Test niche -> non-niche assignment from none
  {
    Maybe<NicheInt> niche;
    Maybe<NonNicheInt> nonNiche = NonNicheInt(99);
    nonNiche = kj::mv(niche);
    KJ_EXPECT(nonNiche == kj::none);
  }

  // Test non-niche -> niche assignment (move)
  {
    Maybe<NonNicheInt> nonNiche = NonNicheInt(42);
    Maybe<NicheInt> niche;
    niche = kj::mv(nonNiche);
    KJ_EXPECT(niche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(niche).value == 42);
  }

  // Test non-niche -> niche assignment (copy)
  {
    Maybe<NonNicheInt> nonNiche = NonNicheInt(42);
    Maybe<NicheInt> niche;
    niche = nonNiche;
    KJ_EXPECT(niche != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(niche).value == 42);
  }

  // Test non-niche -> niche assignment from none
  {
    Maybe<NonNicheInt> nonNiche;
    Maybe<NicheInt> niche = NicheInt(99);
    niche = kj::mv(nonNiche);
    KJ_EXPECT(niche == kj::none);
  }
}

KJ_TEST("Maybe<NoneThrowingNiche> never constructs from none state") {
  // Verify that NullableValue never tries to move/copy construct from a none value.
  // NoneThrowingNiche throws if you try to construct from its none state (value == 0).
  // If any of these operations throw, the test fails.
  static_assert(sizeof(Maybe<NoneThrowingNiche>) == sizeof(NoneThrowingNiche),
      "Should be niche-optimized");

  // Move-construct from empty Maybe
  {
    Maybe<NoneThrowingNiche> empty;
    KJ_EXPECT(empty == kj::none);
    Maybe<NoneThrowingNiche> moved = kj::mv(empty);  // Should NOT throw
    KJ_EXPECT(moved == kj::none);
  }

  // Copy-construct from empty Maybe
  {
    Maybe<NoneThrowingNiche> empty;
    KJ_EXPECT(empty == kj::none);
    Maybe<NoneThrowingNiche> copied = empty;  // Should NOT throw
    KJ_EXPECT(copied == kj::none);
  }

  // Move-assign from empty Maybe
  {
    Maybe<NoneThrowingNiche> empty;
    Maybe<NoneThrowingNiche> target = NoneThrowingNiche(42);
    KJ_EXPECT(target != kj::none);
    target = kj::mv(empty);  // Should NOT throw
    KJ_EXPECT(target == kj::none);
  }

  // Copy-assign from empty Maybe
  {
    Maybe<NoneThrowingNiche> empty;
    Maybe<NoneThrowingNiche> target = NoneThrowingNiche(42);
    KJ_EXPECT(target != kj::none);
    target = empty;  // Should NOT throw
    KJ_EXPECT(target == kj::none);
  }

  // Move-construct from non-empty Maybe (should work, source becomes none)
  {
    Maybe<NoneThrowingNiche> src = NoneThrowingNiche(42);
    Maybe<NoneThrowingNiche> dst = kj::mv(src);
    KJ_EXPECT(dst != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(dst).value == 42);
    // src is now none - Maybe-to-Maybe move always sets source to none.
    // The key point is that NullableValue's move constructor moved from the value WITHOUT
    // throwing (because we don't construct from the none state), and then Maybe set src to none.
    KJ_EXPECT(src == kj::none);
  }

  // Copy-construct from non-empty Maybe
  {
    Maybe<NoneThrowingNiche> src = NoneThrowingNiche(42);
    Maybe<NoneThrowingNiche> dst = src;
    KJ_EXPECT(dst != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(dst).value == 42);
    KJ_EXPECT(src != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(src).value == 42);
  }
}

KJ_TEST("Maybe never destroys none values") {
  // Verify that NullableValue never calls T's destructor on a none value.
  // This is important because it allows T's destructor to assume it is always
  // destroying a valid (non-none) value, simplifying RAII types.
  static_assert(sizeof(Maybe<NoneDestructorCounter>) == sizeof(NoneDestructorCounter),
      "Should be niche-optimized");

  NoneDestructorCounter::nonNoneDestroyCount = 0;
  NoneDestructorCounter::noneDestroyCount = 0;

  // Test 1: Creating and destroying an empty Maybe
  {
    Maybe<NoneDestructorCounter> empty;
    KJ_EXPECT(empty == kj::none);
  }
  KJ_EXPECT(NoneDestructorCounter::noneDestroyCount == 0);
  KJ_EXPECT(NoneDestructorCounter::nonNoneDestroyCount == 0);

  // Test 2: Creating a value via emplace, then destroying it
  {
    Maybe<NoneDestructorCounter> m;
    m.emplace(42);
    KJ_EXPECT(m != kj::none);
  }
  KJ_EXPECT(NoneDestructorCounter::noneDestroyCount == 0);
  KJ_EXPECT(NoneDestructorCounter::nonNoneDestroyCount == 1);

  NoneDestructorCounter::nonNoneDestroyCount = 0;

  // Test 3: Assigning none to a value (should destroy the value, not create a none to destroy)
  {
    Maybe<NoneDestructorCounter> m;
    m.emplace(42);
    m = kj::none;
    KJ_EXPECT(m == kj::none);
  }
  KJ_EXPECT(NoneDestructorCounter::noneDestroyCount == 0);
  KJ_EXPECT(NoneDestructorCounter::nonNoneDestroyCount == 1);  // Just the value, not the none

  NoneDestructorCounter::nonNoneDestroyCount = 0;

  // Test 4: Assigning none to none (should not call any destructor)
  {
    Maybe<NoneDestructorCounter> m;
    m = kj::none;
    KJ_EXPECT(m == kj::none);
  }
  KJ_EXPECT(NoneDestructorCounter::noneDestroyCount == 0);
  KJ_EXPECT(NoneDestructorCounter::nonNoneDestroyCount == 0);

  // Test 5: Emplace on none (should not destroy the none first)
  {
    Maybe<NoneDestructorCounter> m;
    m.emplace(42);
    KJ_EXPECT(m != kj::none);
  }
  KJ_EXPECT(NoneDestructorCounter::noneDestroyCount == 0);
  KJ_EXPECT(NoneDestructorCounter::nonNoneDestroyCount == 1);  // Just the emplaced value

  NoneDestructorCounter::nonNoneDestroyCount = 0;

  // Test 6: Move from Maybe leaves source as none, but doesn't destroy the none
  {
    Maybe<NoneDestructorCounter> src;
    src.emplace(42);
    Maybe<NoneDestructorCounter> dst = kj::mv(src);
    KJ_EXPECT(src == kj::none);
    KJ_EXPECT(dst != kj::none);
  }
  KJ_EXPECT(NoneDestructorCounter::noneDestroyCount == 0);
  // Destructions: src's moved-from value (-1) when assigned to none, then dst's value (42) at end
  KJ_EXPECT(NoneDestructorCounter::nonNoneDestroyCount == 2);

  NoneDestructorCounter::nonNoneDestroyCount = 0;

  // Test 7: Multiple emplace calls (each should destroy the previous value, not any none)
  {
    Maybe<NoneDestructorCounter> m;
    m.emplace(1);
    m.emplace(2);
    m.emplace(3);
  }
  KJ_EXPECT(NoneDestructorCounter::noneDestroyCount == 0);
  KJ_EXPECT(NoneDestructorCounter::nonNoneDestroyCount == 3);  // Values 1, 2, and 3
}

// =======================================================================================
// Exception safety tests for niche-optimized Maybe<T>
//
// These tests verify that if a constructor or destructor throws, the Maybe is left in a
// valid none state. This is important because niche-optimized Maybe doesn't have a separate
// bool flag, so we can't just "mark as unset" - we must actually construct a none value.

// A type that can be configured to throw during construction or destruction.
// Uses value == 0 as the "none" niche state.
// value == -1 is the "moved-from" state (not none, needs destruction).
struct ThrowingNiche {
  int value;
  bool throwOnDestroy = false;

  static bool throwOnConstruct;
  static bool throwOnMovedFromDestroy;  // Throw when destroying moved-from value (value == -1)
  static int constructCount;
  static int destroyCount;

  explicit ThrowingNiche(int v): value(v) {
    ++constructCount;
    if (throwOnConstruct && v != 0) {  // Don't throw for none value (v == 0)
      throw std::runtime_error("constructor throw");
    }
  }

  ThrowingNiche(ThrowingNiche&& other): value(other.value), throwOnDestroy(other.throwOnDestroy) {
    // Set to moved-from state (-1), NOT to none state (0).
    // This means the moved-from object still needs destruction.
    other.value = -1;
    other.throwOnDestroy = false;
    ++constructCount;
    if (throwOnConstruct && value != 0) {
      throw std::runtime_error("move constructor throw");
    }
  }

  ~ThrowingNiche() noexcept(false) {
    ++destroyCount;
    if (throwOnDestroy) {
      throwOnDestroy = false;
      throw std::runtime_error("destructor throw");
    }
    if (throwOnMovedFromDestroy && value == -1) {
      throwOnMovedFromDestroy = false;  // Only throw once
      throw std::runtime_error("moved-from destructor throw");
    }
  }

  KJ_DISALLOW_COPY(ThrowingNiche);

  friend struct MaybeTraits<ThrowingNiche>;
};

bool ThrowingNiche::throwOnConstruct = false;
bool ThrowingNiche::throwOnMovedFromDestroy = false;
int ThrowingNiche::constructCount = 0;
int ThrowingNiche::destroyCount = 0;

}  // namespace (anonymous)

template <>
struct MaybeTraits<ThrowingNiche> {
  // Niche optimization: value == 0 is the "none" state
  static void initNone(ThrowingNiche* ptr) noexcept { kj::ctor(*ptr, 0); }
  static bool isNone(const ThrowingNiche& m) noexcept { return m.value == 0; }
};

namespace {

KJ_TEST("Maybe<ThrowingNiche> exception safety - constructor throws in emplace") {
  // Verify that if ctor throws during emplace, the Maybe is left in none state.
  static_assert(sizeof(Maybe<ThrowingNiche>) == sizeof(ThrowingNiche),
      "Should be niche-optimized");

  ThrowingNiche::throwOnConstruct = false;
  ThrowingNiche::constructCount = 0;
  ThrowingNiche::destroyCount = 0;

  Maybe<ThrowingNiche> m;
  m.emplace(42);
  KJ_EXPECT(m != kj::none);

  // Now make the next constructor throw
  ThrowingNiche::throwOnConstruct = true;

  bool caught = false;
  try {
    m.emplace(99);  // Should throw
  } catch (const std::runtime_error& e) {
    caught = true;
    KJ_EXPECT(kj::StringPtr(e.what()) == "constructor throw");
  }

  KJ_EXPECT(caught);
  KJ_EXPECT(m == kj::none);  // Should be in none state after throw

  ThrowingNiche::throwOnConstruct = false;
}

KJ_TEST("Maybe<ThrowingNiche> exception safety - destructor throws in emplace") {
  // Verify that if dtor throws during emplace, the Maybe is left in none state.
  ThrowingNiche::throwOnConstruct = false;
  ThrowingNiche::constructCount = 0;
  ThrowingNiche::destroyCount = 0;

  Maybe<ThrowingNiche> m;
  m.emplace(42);
  KJ_EXPECT(m != kj::none);

  // Set up the value to throw on destruction
  KJ_ASSERT_NONNULL(m).throwOnDestroy = true;

  bool caught = false;
  try {
    m.emplace(99);  // Destructor of old value should throw
  } catch (const std::runtime_error& e) {
    caught = true;
    KJ_EXPECT(kj::StringPtr(e.what()) == "destructor throw");
  }

  KJ_EXPECT(caught);
  KJ_EXPECT(m == kj::none);  // Should be in none state after throw
}

KJ_TEST("Maybe<ThrowingNiche> exception safety - constructor throws in assignment") {
  // Verify that if ctor throws during assignment, the target Maybe is unchanged (strong exception
  // guarantee). The assignment operator extracts from `other` first into a temp, so if that
  // extraction throws, `this` is unchanged.
  ThrowingNiche::throwOnConstruct = false;
  ThrowingNiche::constructCount = 0;
  ThrowingNiche::destroyCount = 0;

  Maybe<ThrowingNiche> m;
  m.emplace(42);
  KJ_EXPECT(m != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(m).value == 42);

  Maybe<ThrowingNiche> other;
  other.emplace(99);
  KJ_EXPECT(other != kj::none);

  // Make move constructor throw
  ThrowingNiche::throwOnConstruct = true;

  bool caught = false;
  try {
    m = kj::mv(other);  // Move ctor should throw during extraction
  } catch (const std::runtime_error& e) {
    caught = true;
    KJ_EXPECT(kj::StringPtr(e.what()) == "move constructor throw");
  }

  KJ_EXPECT(caught);
  // Strong exception guarantee: m is unchanged
  KJ_EXPECT(m != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(m).value == 42);

  ThrowingNiche::throwOnConstruct = false;
}

KJ_TEST("Maybe<ThrowingNiche> exception safety - destructor throws in assignment") {
  // Verify that if dtor throws during assignment, the Maybe is left in none state.
  ThrowingNiche::throwOnConstruct = false;
  ThrowingNiche::constructCount = 0;
  ThrowingNiche::destroyCount = 0;

  Maybe<ThrowingNiche> m;
  m.emplace(42);
  KJ_EXPECT(m != kj::none);

  // Set up the value to throw on destruction
  KJ_ASSERT_NONNULL(m).throwOnDestroy = true;

  Maybe<ThrowingNiche> other;
  other.emplace(99);
  KJ_EXPECT(other != kj::none);

  bool caught = false;
  try {
    m = kj::mv(other);  // Destructor of old value should throw
  } catch (const std::runtime_error& e) {
    caught = true;
    KJ_EXPECT(kj::StringPtr(e.what()) == "destructor throw");
  }

  KJ_EXPECT(caught);
  KJ_EXPECT(m == kj::none);  // Should be in none state after throw
}

KJ_TEST("Maybe<ThrowingNiche> exception safety - destructor throws in assign-to-none") {
  // Verify that if dtor throws when assigning to none, the Maybe is left in none state.
  ThrowingNiche::throwOnConstruct = false;
  ThrowingNiche::constructCount = 0;
  ThrowingNiche::destroyCount = 0;

  Maybe<ThrowingNiche> m;
  m.emplace(42);
  KJ_EXPECT(m != kj::none);

  // Set up the value to throw on destruction
  KJ_ASSERT_NONNULL(m).throwOnDestroy = true;

  bool caught = false;
  try {
    m = kj::none;  // Destructor should throw
  } catch (const std::runtime_error& e) {
    caught = true;
    KJ_EXPECT(kj::StringPtr(e.what()) == "destructor throw");
  }

  KJ_EXPECT(caught);
  KJ_EXPECT(m == kj::none);  // Should be in none state after throw
}

KJ_TEST("Maybe<ThrowingNiche> exception safety - source destructor throws in move-assignment") {
  // Verify that if the source's dtor throws during move-assignment, we don't have memory
  // corruption or double-frees.
  //
  // With the current implementation, move-assignment works by:
  // 1. Move src into a temp Maybe
  // 2. Emplace dst from temp
  //
  // The Maybe move constructor sets src to none after moving, which destroys the moved-from
  // value. If that destructor throws, the exception happens during step 1.
  //
  // After the exception:
  // - dst is unchanged (strong exception guarantee)
  // - src may be in an indeterminate state (but shouldn't be double-destroyed)
  ThrowingNiche::throwOnConstruct = false;
  ThrowingNiche::throwOnMovedFromDestroy = false;
  ThrowingNiche::constructCount = 0;
  ThrowingNiche::destroyCount = 0;

  Maybe<ThrowingNiche> src;
  src.emplace(42);

  Maybe<ThrowingNiche> dst;

  // Set up to throw when destroying the moved-from value (value == -1).
  ThrowingNiche::throwOnMovedFromDestroy = true;

  bool caught = false;
  try {
    dst = kj::mv(src);  // Move throws when destroying moved-from src in temp construction
  } catch (const std::runtime_error& e) {
    caught = true;
    KJ_EXPECT(kj::StringPtr(e.what()) == "moved-from destructor throw");
  }

  KJ_EXPECT(caught);
  // dst is unchanged (strong exception guarantee)
  KJ_EXPECT(dst == kj::none);
  // src may be in a moved-from state, but the test passing (no crash) means no double-free

  ThrowingNiche::throwOnMovedFromDestroy = false;  // Clean up
}

// =======================================================================================
// Tests for self-ownership safety in Maybe assignment operators
//
// These tests verify that Maybe's assignment operators are safe when `this` owns `other`.
// For example: head = kj::mv(head->next) where head owns the node containing next.
// Without proper handling, destroying `this`'s old value before accessing `other` causes
// use-after-free.

KJ_TEST("Maybe<Own<T>> move-assignment is safe when this owns other") {
  // Test: head = kj::mv(head->next) where head owns the node containing next.
  // This is niche-optimized (Own<T> uses nullptr as none state).

  struct ListNode {
    int value;
    Maybe<Own<ListNode>> next;
    ListNode(int v): value(v) {}
  };

  Maybe<Own<ListNode>> head = heap<ListNode>(1);
  KJ_ASSERT_NONNULL(head)->next = heap<ListNode>(2);
  KJ_ASSERT_NONNULL(KJ_ASSERT_NONNULL(head)->next)->next = heap<ListNode>(3);

  // Advance head to next - this would be use-after-free without a correctly implemented
  // assignment operator
  KJ_IF_SOME(node, head) {
    head = kj::mv(node->next);
  }

  KJ_EXPECT(head != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(head)->value == 2);

  // Do it again
  KJ_IF_SOME(node, head) {
    head = kj::mv(node->next);
  }

  KJ_EXPECT(head != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(head)->value == 3);

  // And once more - should become none
  KJ_IF_SOME(node, head) {
    head = kj::mv(node->next);
  }

  KJ_EXPECT(head == kj::none);
}

KJ_TEST("Maybe<Own<T>> copy-assignment is safe when this owns other") {
  // Test copy-assignment where `other` is inside `this`'s value.

  struct CopyableNode {
    int value;
    Maybe<Own<CopyableNode>> next;

    CopyableNode(int v): value(v) {}
    CopyableNode(const CopyableNode& other): value(other.value) {
      KJ_IF_SOME(n, other.next) {
        next = heap<CopyableNode>(*n);
      }
    }
    CopyableNode& operator=(const CopyableNode&) = default;
  };

  Maybe<CopyableNode> head = CopyableNode(1);
  KJ_ASSERT_NONNULL(head).next = heap<CopyableNode>(2);

  // Copy-assign from a value inside this - would be use-after-free without fix
  KJ_IF_SOME(node, head) {
    KJ_IF_SOME(nextNode, node.next) {
      head = *nextNode;  // Copy from *next into head
    }
  }

  KJ_EXPECT(head != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(head).value == 2);
}

KJ_TEST("Maybe<T> T-value move-assignment is safe when this owns other") {
  // Test operator=(T&&) where the T value is inside this's current value.

  struct Node {
    int value;
    Maybe<Own<Node>> next;
    Node(int v): value(v) {}
  };

  // Create a Maybe that owns a Node, which owns another Node
  Maybe<Node> head = Node(1);
  KJ_ASSERT_NONNULL(head).next = heap<Node>(2);

  // Move-assign from the inner Node to head
  KJ_IF_SOME(node, head) {
    KJ_IF_SOME(next, node.next) {
      head = kj::mv(*next);  // operator=(T&&) where T is inside head's value
    }
  }

  KJ_EXPECT(head != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(head).value == 2);
}

KJ_TEST("Maybe<T> T-value copy-assignment is safe when this owns other") {
  // Test operator=(const T&) where the T value is inside this's current value.

  struct Node {
    int value;
    Maybe<Own<Node>> next;
    Node(int v): value(v) {}
    Node(const Node& other): value(other.value) {
      // Deep copy
      KJ_IF_SOME(n, other.next) {
        next = heap<Node>(*n);
      }
    }
    Node& operator=(const Node&) = default;
  };

  // Create a Maybe that owns a Node, which owns another Node
  Maybe<Node> head = Node(1);
  KJ_ASSERT_NONNULL(head).next = heap<Node>(2);

  // Copy-assign from the inner Node to head
  KJ_IF_SOME(node, head) {
    KJ_IF_SOME(next, node.next) {
      head = *next;  // operator=(const T&) where T is inside head's value
    }
  }

  KJ_EXPECT(head != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(head).value == 2);
}

// =======================================================================================
// Cross-type assignment tests

KJ_TEST("Maybe<StringPtr> assigned from Maybe<String>") {
  // String is not copyable, so cross-type assignment must not try to copy the Maybe<String>.
  // Instead it should convert String -> StringPtr directly.

  // Copy-assignment from const Maybe<String>&
  Maybe<String> s = kj::str("hello");
  Maybe<StringPtr> sp;
  sp = s;
  KJ_EXPECT(KJ_ASSERT_NONNULL(sp) == "hello");
  KJ_EXPECT(s != kj::none);  // source unchanged

  // Note: Move-assignment (sp = kj::mv(s)) would leave sp with a dangling StringPtr because
  // StringPtr is non-owning and the String would be destroyed. This is expected behavior.
}

KJ_TEST("Cross-type Maybe assignment with move-only types") {
  // Test that cross-type assignment works with move-only types.
  // Own<Derived> -> Own<Base> is a common case.

  struct Base {
    int value;
    explicit Base(int v): value(v) {}
    virtual ~Base() = default;
  };
  struct Derived: Base {
    explicit Derived(int v): Base(v) {}
  };

  {
    // Move-assignment from Maybe<Own<Derived>>&& to Maybe<Own<Base>>
    Maybe<Own<Derived>> derived = heap<Derived>(42);
    Maybe<Own<Base>> base;
    base = kj::mv(derived);
    KJ_EXPECT(KJ_ASSERT_NONNULL(base)->value == 42);
    KJ_EXPECT(derived == kj::none);  // source nullified
  }

  {
    // Move-assignment when target already has a value
    Maybe<Own<Derived>> derived = heap<Derived>(99);
    Maybe<Own<Base>> base = heap<Base>(1);
    base = kj::mv(derived);
    KJ_EXPECT(KJ_ASSERT_NONNULL(base)->value == 99);
    KJ_EXPECT(derived == kj::none);
  }
}

KJ_TEST("Maybe self-assignment is safe") {
  // Test that self-assignment (m = m) works correctly.

  {
    // Move self-assignment
    Maybe<int> m = 42;
    m = kj::mv(m);
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m) == 42);
  }

  {
    // Copy self-assignment
    Maybe<int> m = 42;
    Maybe<int>& ref = m;
    m = ref;
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m) == 42);
  }

  {
    // Self-assignment of none
    Maybe<int> m;
    m = kj::mv(m);
    KJ_EXPECT(m == kj::none);
  }
}

}  // namespace
}  // namespace kj
