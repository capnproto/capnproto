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

#include "refcount.h"
// StringPtr is only forward-declared at this point; its pointer-type registration must already be
// visible so that Rc<StringPtr> has the same layout in every translation unit.
static_assert(kj::isPointerType<kj::StringPtr>());
#include "array.h"
#include "string.h"
#include "thread.h"
#include "mutex.h"
#include <kj/compat/gtest.h>

#include <atomic>
#include <thread>

namespace kj {

namespace _ {
struct PointerConversionSource {
  const int* ptr;
};

template <bool isNoexcept>
struct PointerConversionTarget {
  PointerConversionTarget(const PointerConversionSource& source) noexcept(isNoexcept)
      : ptr(source.ptr) {}
  const int* ptr;
};
}  // namespace _

template <>
struct PointerTraits<_::PointerConversionSource> {
  static constexpr bool isPointer = true;
  static constexpr bool isReadOnly = true;
};

template <bool isNoexcept>
struct PointerTraits<_::PointerConversionTarget<isNoexcept>> {
  static constexpr bool isPointer = true;
  static constexpr bool isReadOnly = true;
};

namespace _ {
// Both targets have noexcept same-type copies/moves, but only one has a noexcept conversion.
static_assert(isNoThrowMoveConstructible<PointerConversionTarget<false>>());
static_assert(canConvert<PointerConversionSource, PointerConversionTarget<false>>());
static_assert(!canConvert<Rc<PointerConversionSource>, Rc<PointerConversionTarget<false>>>());
static_assert(!canConvert<Arc<PointerConversionSource>, Arc<PointerConversionTarget<false>>>());
static_assert(!canConvert<Rc<const PointerConversionSource>, Rc<PointerConversionTarget<false>>>());
static_assert(!canConvert<Arc<const PointerConversionSource>,
                          Arc<PointerConversionTarget<false>>>());
static_assert(canConvert<Rc<PointerConversionSource>, Rc<PointerConversionTarget<true>>>());
static_assert(canConvert<Arc<PointerConversionSource>, Arc<PointerConversionTarget<true>>>());

static_assert(isPointerType<ArrayPtr<int>>());
static_assert(isPointerType<StringPtr>());
static_assert(!isPointerType<Array<int>>());
static_assert(!isPointerType<String>());
static_assert(!isPointerType<int*>());
static_assert(!isProjectionResult<int>());
static_assert(!isProjectionResult<int&&>());
static_assert(!isProjectionResult<void>());
static_assert(!isProjectionResult<ArrayPtr<int>&&>());
static_assert(isProjectionResult<ArrayPtr<int>&>());
static_assert(!isProjectionResult<const ArrayPtr<int>&>());
static_assert(isProjectionResult<const ArrayPtr<const int>&>());
static_assert(sizeof(Rc<ArrayPtr<int>>) == sizeof(void*) + sizeof(ArrayPtr<int>));
static_assert(!canConvert<ArrayPtr<int>, Rc<ArrayPtr<int>>>());

// Read-only pointer types are exposed as const and cannot be re-pointed through *rc. Mutable ones
// stay non-const so that their non-const accessors (e.g. builder setters) remain usable.
template <typename R, typename P>
concept CanAssignThroughRc = requires(R& rc, P p) { *rc = p; };
static_assert(!CanAssignThroughRc<Rc<StringPtr>, StringPtr>);
static_assert(!CanAssignThroughRc<Rc<ArrayPtr<const int>>, ArrayPtr<const int>>);
static_assert(CanAssignThroughRc<Rc<ArrayPtr<int>>, ArrayPtr<int>>);
static_assert(isSameType<decltype(*instance<Rc<StringPtr>&>()), const StringPtr&>());
static_assert(isSameType<decltype(instance<Rc<StringPtr>&>().get()), const StringPtr*>());

KJ_TEST("Rc and Arc transfer ownership through noexcept pointer conversions") {
  auto source = kj::rc<int>(123).project([](auto& value) {
    return PointerConversionSource{&value};
  });
  Rc<PointerConversionTarget<true>> target(kj::mv(source));
  KJ_EXPECT(source == nullptr);
  KJ_EXPECT(*target->ptr == 123);

  auto atomicSource = kj::arc<int>(456).project([](auto& value) {
    return PointerConversionSource{&value};
  });
  Arc<PointerConversionTarget<true>> atomicTarget(kj::mv(atomicSource));
  KJ_EXPECT(atomicSource == nullptr);
  KJ_EXPECT(*atomicTarget->ptr == 456);
}

KJ_TEST("Rc const-qualified mutable pointer types can addRef and clone") {
  auto ptr = kj::rc<Array<int>>(kj::heapArray<int>({12, 34})).project(
      [](auto& array) { return array.asPtr(); });
  Rc<const ArrayPtr<int>> frozen(kj::mv(ptr));
  auto copy = frozen.addRef();
  auto clone = frozen.clone();
  static_assert(isSameType<decltype(copy), Rc<const ArrayPtr<int>>>());
  static_assert(isSameType<decltype((*copy)[0]), const int&>());
  KJ_EXPECT(copy.get() != frozen.get());
  KJ_EXPECT(copy->begin() == frozen->begin());
  frozen = nullptr;
  KJ_EXPECT((*copy)[0] == 12);
  copy = nullptr;
  KJ_EXPECT((*clone)[1] == 34);

  auto readonly = kj::mv(clone).project([](auto& array) { return array.asConst(); });
  auto identity = kj::mv(readonly).project([](const auto& array) -> const auto& {
    return array;
  });
  KJ_EXPECT((*identity.clone())[1] == 34);
  KJ_EXPECT(frozen.addRef() == nullptr);
}

KJ_TEST("Rc stores pointer types inline and projects slices and elements") {
  auto owner = kj::rc<Array<int>>(kj::heapArray<int>({10, 20, 30}));
  auto ptr = owner.addRef().project([](auto& array) { return array.asPtr(); });
  static_assert(isSameType<decltype(ptr), Rc<ArrayPtr<int>>>());
  auto sibling = ptr.addRef();
  KJ_EXPECT(ptr.get() != sibling.get());
  KJ_EXPECT(ptr->begin() == sibling->begin());
  auto moved = kj::mv(ptr);
  KJ_EXPECT(ptr == nullptr);
  auto identity = kj::mv(moved).project([](auto& ptr) -> auto& { return ptr; });
  KJ_EXPECT((*identity)[1] == 20);
  auto slice = kj::mv(identity).project([](auto& ptr) { return ptr.slice(1); });
  auto element = kj::mv(slice).project([](auto& ptr) -> int& { return ptr[0]; });
  owner = nullptr;
  sibling = nullptr;
  KJ_EXPECT(*element == 20);
  *element = 42;
}

KJ_TEST("Rc and Arc empty pointers retain ownership and distinguish null handles") {
  struct Owner {
    Owner(bool& destroyed): destroyed(destroyed) {}
    ~Owner() { destroyed = true; }
    bool& destroyed;
  };
  bool destroyed = false;
  auto owner = kj::rc<Owner>(destroyed);
  auto empty = kj::mv(owner).project([](auto&) { return ArrayPtr<const int>(); });
  KJ_EXPECT(empty != nullptr);
  KJ_EXPECT(empty->size() == 0);
  auto clone = empty.addRef();
  empty = nullptr;
  KJ_EXPECT(!destroyed);
  KJ_EXPECT(clone->size() == 0);
  clone = nullptr;
  KJ_EXPECT(destroyed);

  auto atomicOwner = kj::arc<Array<int>>(kj::heapArray<int>(0));
  auto atomicEmpty = kj::mv(atomicOwner).project([](auto& array) { return array.asPtr(); });
  KJ_EXPECT(atomicEmpty != nullptr);
  KJ_EXPECT(atomicEmpty.addRef()->size() == 0);
  atomicEmpty = nullptr;
  KJ_EXPECT(atomicEmpty == nullptr);
}

KJ_TEST("inline pointer conversions and Own adoption retain the backing array") {
  auto owner = kj::rc<Array<int>>(kj::heapArray<int>({1, 2, 3}));
  auto mutablePtr = kj::mv(owner).project([](auto& array) { return array.asPtr(); });
  Rc<ArrayPtr<const int>> ptr(kj::mv(mutablePtr));
  KJ_EXPECT(mutablePtr == nullptr);
  KJ_EXPECT((*ptr)[2] == 3);

  // toOwn() is unavailable for pointer types; an Own that carries its owner must be built
  // explicitly, and can then be adopted back into an Rc.
  Own<ArrayPtr<const int>> own = kj::attachVal(ArrayPtr<const int>(*ptr), kj::mv(ptr));
  KJ_EXPECT(ptr == nullptr);
  Rc<ArrayPtr<const int>> adopted(kj::mv(own));
  KJ_EXPECT((*adopted)[2] == 3);
  adopted = nullptr;

  auto string = kj::arc<String>(kj::str("hello"));
  auto text = kj::mv(string).project([](auto& value) { return value.asPtr(); });
  Arc<ArrayPtr<const char>> bytes = text.addRef().project(
      [](auto& value) { return value.asArray(); });
  Own<const ArrayPtr<const char>> atomicOwn =
      kj::attachVal(ArrayPtr<const char>(*bytes), kj::mv(bytes));
  KJ_EXPECT(bytes == nullptr);
  text = nullptr;
  Arc<ArrayPtr<const char>> atomicAdopted(kj::mv(atomicOwn));
  KJ_EXPECT((*atomicAdopted)[1] == 'e');
}

KJ_TEST("pointer projections guard callbacks and clean up on exceptions") {
  auto owner = kj::rc<Array<int>>(kj::heapArray<int>({123, 456}));
  auto source = kj::mv(owner).project([](auto& array) { return array.asPtr(); });
  auto projected = kj::mv(source).project([&](auto& ptr) {
    source = nullptr;
    KJ_EXPECT(ptr[0] == 123);
    return ptr.slice(1);
  });
  KJ_EXPECT((*projected)[0] == 456);
  KJ_EXPECT_THROW_MESSAGE("projection failed", kj::mv(projected).project(
      [](auto& ptr) -> ArrayPtr<int> {
    KJ_FAIL_REQUIRE("projection failed");
  }));
  KJ_EXPECT(projected == nullptr);
#if defined(KJ_DEBUG) || (defined(KJ_ENABLE_IREQUIRE) && KJ_ENABLE_IREQUIRE)
  bool called = false;
  KJ_EXPECT_THROW_MESSAGE("null Rc<> projection", kj::mv(projected).project(
      [&](auto& ptr) { called = true; return ptr.slice(0); }));
  KJ_EXPECT(!called);
#endif

  auto string = kj::arc<String>(kj::str("abc"));
  auto atomicPtr = kj::mv(string).project([](auto& string) { return string.asPtr(); });
  auto atomicSlice = kj::mv(atomicPtr).project([&](auto& ptr) {
    atomicPtr = nullptr;
    return ptr.slice(1);
  });
  KJ_EXPECT(*atomicSlice == "bc");
  KJ_EXPECT_THROW_MESSAGE("projection failed", kj::mv(atomicSlice).project(
      [](auto& ptr) -> StringPtr { KJ_FAIL_REQUIRE("projection failed"); }));
  KJ_EXPECT(atomicSlice == nullptr);
}

KJ_TEST("Arc pointer clones can project and release on another thread") {
  auto owner = kj::arc<Array<int>>(kj::heapArray<int>({11, 22, 33}));
  auto ptr = kj::mv(owner).project([](auto& array) { return array.asPtr(); });
  Thread worker([copy = ptr.addRef()]() mutable {
    auto slice = kj::mv(copy).project([](auto& array) { return array.slice(1); });
    auto element = kj::mv(slice).project([](auto& array) -> const int& { return array[1]; });
    KJ_EXPECT(*element == 33);
  });
  ptr = nullptr;
}

struct SetTrueInDestructor: public Refcounted {
  SetTrueInDestructor(bool* ptr): ptr(ptr) {}
  ~SetTrueInDestructor() { *ptr = true; }

  kj::Rc<SetTrueInDestructor> newRef() { return addRefToThis(); }
  kj::WeakRc<SetTrueInDestructor> newWeakRef() { return addWeakToThis(); }

  bool* ptr;
};

static_assert(Cloneable<Rc<SetTrueInDestructor>>);
static_assert(!Cloneable<const Rc<SetTrueInDestructor>>);
static_assert(Cloneable<Maybe<Rc<SetTrueInDestructor>>>);
static_assert(!Cloneable<const Maybe<Rc<SetTrueInDestructor>>>);
static_assert(Cloneable<Array<Rc<SetTrueInDestructor>>>);
static_assert(!Cloneable<const Array<Rc<SetTrueInDestructor>>>);
static_assert(Cloneable<ArrayPtr<Rc<SetTrueInDestructor>>>);
static_assert(!Cloneable<const ArrayPtr<Rc<SetTrueInDestructor>>>);

struct ProjectionTarget {
  ProjectionTarget(bool* destroyed, int value): destroyed(destroyed), value(value) {}
  ~ProjectionTarget() { *destroyed = true; }

  bool* destroyed;
  int value;
};

struct WeakInConstructor: public Refcounted {
  // Captures a weak reference to itself from within its constructor, exercising addWeakToThis()
  // before kj::rc()/kj::refcounted() has incremented the refcount.
  WeakInConstructor(bool* ptr): ptr(ptr), weak(addWeakToThis()) {}
  ~WeakInConstructor() { *ptr = true; }

  bool* ptr;
  kj::WeakRc<WeakInConstructor> weak;
};

struct IncompleteDeclaredRefcounted;
static_assert(sizeof(Rc<IncompleteDeclaredRefcounted>) == 2 * sizeof(void*));

struct IncompleteDeclaredRefcounted: public Refcounted {
  IncompleteDeclaredRefcounted(bool* ptr): ptr(ptr) {}
  ~IncompleteDeclaredRefcounted() { *ptr = true; }

  bool* ptr;
};

struct IncompleteDeclaredNotRefcounted;
static_assert(sizeof(Rc<IncompleteDeclaredNotRefcounted>) == 2 * sizeof(void*));

struct IncompleteDeclaredNotRefcounted {
  IncompleteDeclaredNotRefcounted(bool* ptr): ptr(ptr) {}
  ~IncompleteDeclaredNotRefcounted() { *ptr = true; }

  bool* ptr;
};

struct IncompleteInnerDeclaredRefcounted {
private:
  struct Inner;
  static_assert(sizeof(Rc<Inner>) == 2 * sizeof(void*));

public:
  static void test();
};

struct IncompleteInnerDeclaredRefcounted::Inner: public Refcounted {
  Inner(bool* ptr): ptr(ptr) {}
  ~Inner() { *ptr = true; }

  bool* ptr;
};

void IncompleteInnerDeclaredRefcounted::test() {
  bool b = false;
  Rc<Inner> ref = kj::rc<Inner>(&b);
  KJ_EXPECT(!b);
  ref = nullptr;
  KJ_EXPECT(b);
}

struct IncompleteInnerDeclaredNotRefcounted {
private:
  struct Inner;
  static_assert(sizeof(Rc<Inner>) == 2 * sizeof(void*));

public:
  static void test();
};

struct IncompleteInnerDeclaredNotRefcounted::Inner {
  Inner(bool* ptr): ptr(ptr) {}
  ~Inner() { *ptr = true; }

  bool* ptr;
};

void IncompleteInnerDeclaredNotRefcounted::test() {
  bool b = false;
  Rc<Inner> ref = kj::rc<Inner>(&b);
  KJ_EXPECT(!b);
  auto ref2 = ref.addRef();
  ref = nullptr;
  KJ_EXPECT(!b);
  ref2 = nullptr;
  KJ_EXPECT(b);
}

KJ_TEST("Rc incomplete declared refcounted types") {
  {
    bool b = false;
    Rc<IncompleteDeclaredRefcounted> ref = kj::rc<IncompleteDeclaredRefcounted>(&b);
    KJ_EXPECT(!b);
    ref = nullptr;
    KJ_EXPECT(b);
  }

  IncompleteInnerDeclaredRefcounted::test();
}

KJ_TEST("Rc incomplete declared non-refcounted types") {
  {
    bool b = false;
    Rc<IncompleteDeclaredNotRefcounted> ref = kj::rc<IncompleteDeclaredNotRefcounted>(&b);
    KJ_EXPECT(!b);
    auto ref2 = ref.addRef();
    ref = nullptr;
    KJ_EXPECT(!b);
    ref2 = nullptr;
    KJ_EXPECT(b);
  }

  IncompleteInnerDeclaredNotRefcounted::test();
}

TEST(Refcount, Basic) {
  bool b = false;
  Own<SetTrueInDestructor> ref1 = kj::refcounted<SetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());
  Own<SetTrueInDestructor> ref2 = kj::addRef(*ref1);
  EXPECT_TRUE(ref1->isShared());
  Own<SetTrueInDestructor> ref3 = kj::addRef(*ref2);
  EXPECT_TRUE(ref1->isShared());

  EXPECT_FALSE(b);
  ref1 = Own<SetTrueInDestructor>();
  EXPECT_TRUE(ref2->isShared());
  EXPECT_FALSE(b);
  ref3 = Own<SetTrueInDestructor>();
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);
  ref2 = Own<SetTrueInDestructor>();
  EXPECT_TRUE(b);

#ifdef KJ_DEBUG
  b = false;
  // A Refcounted object is born with refcount == 1 and must be adopted by the Own/Rc returned from
  // kj::refcounted()/kj::rc(). Allocating one another way (e.g. on the stack) and destroying it
  // trips the destructor's refcount assertion, since the count never returns to zero via disposal.
  EXPECT_ANY_THROW(SetTrueInDestructor obj(&b));
#endif
}

struct InlineRefcounted {
  // Holds a Refcounted object inline as a member, an allocation the destructor should reject.
  InlineRefcounted(bool* ptr): inner(ptr) {}
  SetTrueInDestructor inner;
};

#ifdef KJ_DEBUG
KJ_TEST("Refcounted rejects stack/inline allocation") {
  // A Refcounted object is born with refcount == 1 and only reaches its destructor with refcount 0
  // after being adopted by (and disposed through) an Own<T>/Rc<T>. Allocating it any other way
  // leaves the initial reference stranded, so destruction trips the assertion. This assertion is
  // KJ_DASSERT (debug-only), so this test only runs under KJ_DEBUG.

  bool b = false;

  // Directly on the stack.
  KJ_EXPECT_THROW_MESSAGE("Refcounted object deleted with non-zero refcount", SetTrueInDestructor obj(&b));

  // Inline as a member of another object.
  KJ_EXPECT_THROW_MESSAGE("Refcounted object deleted with non-zero refcount", InlineRefcounted obj(&b));

  // On the heap via plain `new`/`delete` rather than kj::refcounted(). The initial reference is
  // still stranded, so deleting it (which invokes ~Refcounted() with refcount == 1, and not during
  // an unwind) trips the assertion.
  KJ_EXPECT_THROW_MESSAGE("Refcounted object deleted with non-zero refcount",
      delete new SetTrueInDestructor(&b));
}
#endif

struct ThrowInConstructor: public Refcounted {
  // Throws from its constructor body, after the Refcounted base subobject has been fully
  // constructed (with refcount == 1). During the resulting stack unwind, ~Refcounted() runs while
  // refcount is still non-zero; the destructor's assertion must NOT fire spuriously in this case,
  // because we are unwinding due to an exception rather than leaking a stranded reference.
  ThrowInConstructor() {
    KJ_FAIL_ASSERT("throw from Refcounted constructor");
  }
};

KJ_TEST("Refcounted constructor that throws does not trip destructor assertion") {
  // The exception that propagates must be the constructor's, not a secondary failure from the
  // destructor's refcount assertion (which, if it fired while already unwinding, would terminate).
  KJ_EXPECT_THROW_MESSAGE("throw from Refcounted constructor",
      kj::refcounted<ThrowInConstructor>());
}

struct ThrowAfterPublishingWeak: public Refcounted {
  ThrowAfterPublishingWeak(WeakRc<ThrowAfterPublishingWeak>& published) {
    published = addWeakToThis();
    KJ_FAIL_ASSERT("throw after publishing weak reference");
  }
};

KJ_TEST("WeakRc published by throwing constructor expires") {
  WeakRc<ThrowAfterPublishingWeak> weak = nullptr;

  KJ_EXPECT_THROW_MESSAGE("throw after publishing weak reference",
      kj::rc<ThrowAfterPublishingWeak>(weak));

  // A failed construction has ended the referent's lifetime, so the published weak reference must
  // not retain the stale pointer or attempt to read the freed Refcounted object while upgrading.
  KJ_EXPECT(weak == nullptr);
  KJ_EXPECT(weak.upgrade() == kj::none);
}

KJ_TEST("Rc") {
  bool b = false;

  Rc<SetTrueInDestructor> ref1 = kj::rc<SetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());
  EXPECT_TRUE(&*ref1 == ref1.get());
  const auto& cref1 = ref1;
  EXPECT_TRUE(&*cref1 == ref1.get());
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(ref1 == nullptr);

  Rc<SetTrueInDestructor> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1->isShared());
  EXPECT_TRUE(ref1 == ref2);

  {
    Rc<SetTrueInDestructor> ref3 = ref2.addRef();
    EXPECT_TRUE(ref3->isShared());
    // ref3 is dropped
  }

  EXPECT_FALSE(b);

  // start dropping references one by one

  EXPECT_TRUE(ref2->isShared());
  ref1 = nullptr;
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);
  EXPECT_FALSE(ref1 == ref2);

  ref2 = nullptr;
  EXPECT_TRUE(ref1 == ref2);

  // last reference dropped, SetTrueInDestructor destructor should execute
  EXPECT_TRUE(b);
}

KJ_TEST("Rc clone") {
  bool b = false;

  auto ref1 = kj::rc<SetTrueInDestructor>(&b);
  auto ref2 = ref1.clone();

  EXPECT_TRUE(ref1 == ref2);

  ref1 = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Rc project retains ownership of the original object") {
  bool destroyed = false;
  auto ref = kj::rc<ProjectionTarget>(&destroyed, 123);
  auto other = ref.addRef();
  int* value = &ref->value;
  auto projectionSource = ref.addRef();

  Rc<int> projected = kj::mv(projectionSource).project([](ProjectionTarget& target) -> int& {
    return target.value;
  });

  KJ_EXPECT(projectionSource == nullptr);
  KJ_EXPECT(ref != nullptr);
  KJ_EXPECT(projected.get() == value);
  KJ_EXPECT(*projected == 123);
  *projected = 456;
  KJ_EXPECT(ref->value == 456);
  KJ_EXPECT(other->value == 456);

  other = nullptr;
  ref = nullptr;
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);

#if defined(KJ_ENABLE_IREQUIRE) && KJ_ENABLE_IREQUIRE
  Rc<ProjectionTarget> nullRef;
  bool called = false;
  KJ_EXPECT_THROW_MESSAGE("null Rc<> projection",
      nullRef.addRef().project([&](ProjectionTarget& target) -> int& {
    called = true;
    return target.value;
  }));
  KJ_EXPECT(!called);
#endif
}

KJ_TEST("Rc project retains ownership while invoking callback") {
  bool destroyed = false;
  auto foo = kj::rc<ProjectionTarget>(&destroyed, 123);

  Rc<int> projected = foo.addRef().project([&](ProjectionTarget& f) -> int& {
    foo = nullptr;
    KJ_EXPECT(!destroyed);
    return f.value;
  });

  KJ_EXPECT(foo == nullptr);
  KJ_EXPECT(*projected == 123);
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Rc project retains ownership when callback nullifies source") {
  bool destroyed = false;
  auto foo = kj::rc<ProjectionTarget>(&destroyed, 123);

  Rc<int> projected = kj::mv(foo).project([&](ProjectionTarget& f) -> int& {
    KJ_EXPECT(foo == nullptr);
    foo = nullptr;
    KJ_REQUIRE(!destroyed);
    return f.value;
  });

  KJ_EXPECT(foo == nullptr);
  KJ_EXPECT(*projected == 123);
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Rc project supports identity") {
  bool destroyed = false;
  auto ref = kj::rc<ProjectionTarget>(&destroyed, 123);
  auto original = ref.get();

  auto projected = kj::mv(ref).project(
      [](ProjectionTarget& target) -> ProjectionTarget& { return target; });

  KJ_EXPECT(ref == nullptr);
  KJ_EXPECT(projected.get() == original);
  KJ_EXPECT(projected->value == 123);
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Rc supports projections deeper than one level") {
  struct Inner { int value = 123; };
  struct Middle { Inner inner; };
  struct Outer { int prefix = 0; Middle middle; };

  auto outer = kj::rc<Outer>();
  auto middle = outer.addRef().project([](Outer& outer) -> Middle& { return outer.middle; });
  auto inner = middle.addRef().project([](Middle& middle) -> Inner& { return middle.inner; });
  auto value = inner.addRef().project([](Inner& inner) -> int& { return inner.value; });

  KJ_EXPECT(*value == 123);
}

KJ_TEST("WeakRc preserves an Rc projection") {
  bool destroyed = false;
  auto ref = kj::rc<ProjectionTarget>(&destroyed, 123);
  auto projected = ref.addRef().project([](ProjectionTarget& target) -> int& {
    return target.value;
  });
  int* value = projected.get();
  WeakRc<int> weak = projected.downgrade();

  KJ_EXPECT(&weak.assertLive() == value);
  KJ_EXPECT(weak.assertLive() == 123);

  // Any strong reference to the original object keeps the projected weak reference live, even
  // after the projected strong reference itself is dropped.
  projected = nullptr;
  KJ_EXPECT(!destroyed);
  KJ_EXPECT(&weak.assertLive() == value);

  Rc<int> upgraded;
  KJ_IF_SOME(strong, weak.upgrade()) {
    upgraded = kj::mv(strong);
  } else {
    KJ_FAIL_EXPECT("expected projected WeakRc to upgrade");
  }
  KJ_EXPECT(upgraded.get() == value);

  ref = nullptr;
  KJ_EXPECT(!destroyed);
  KJ_EXPECT(weak != nullptr);
  KJ_EXPECT(*upgraded == 123);

  upgraded = nullptr;
  KJ_EXPECT(destroyed);
  KJ_EXPECT(weak == nullptr);
  KJ_EXPECT(weak.upgrade() == kj::none);
}

KJ_TEST("Rc self-assignment") {
  bool b = false;

  Rc<SetTrueInDestructor> ref = kj::rc<SetTrueInDestructor>(&b);
  auto ptr = ref.get();
  auto refPtr = &ref;

  ref = kj::mv(*refPtr);

  EXPECT_TRUE(ref != nullptr);
  EXPECT_TRUE(ref.get() == ptr);
  EXPECT_FALSE(b);

  ref = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Rc container clone") {
  bool b = false;

  {
    auto ref = kj::rc<SetTrueInDestructor>(&b);

    Maybe<Rc<SetTrueInDestructor>> maybe = ref.addRef();
    auto maybeClone = maybe.clone();
    ASSERT_TRUE(maybeClone != kj::none);
    EXPECT_TRUE(KJ_ASSERT_NONNULL(maybe) == KJ_ASSERT_NONNULL(maybeClone));

    ArrayBuilder<Rc<SetTrueInDestructor>> builder = heapArrayBuilder<Rc<SetTrueInDestructor>>(2);
    builder.add(ref.addRef());
    builder.add(ref.addRef());
    auto array = builder.finish();

    auto arrayPtr = array.asPtr();
    auto arrayPtrClone = arrayPtr.clone();
    ASSERT_EQ(2u, arrayPtrClone.size());
    EXPECT_TRUE(arrayPtrClone[0] == array[0]);
    EXPECT_TRUE(arrayPtrClone[1] == array[1]);

    auto arrayClone = array.clone();
    ASSERT_EQ(2u, arrayClone.size());
    EXPECT_TRUE(arrayClone[0] == array[0]);
    EXPECT_TRUE(arrayClone[1] == array[1]);
  }

  EXPECT_TRUE(b);
}

KJ_TEST("Rc Own interop") {
    bool b = false;

    Rc<SetTrueInDestructor> ref1 = kj::rc<SetTrueInDestructor>(&b);

    EXPECT_FALSE(b);
    auto own = ref1.toOwn();
    EXPECT_TRUE(ref1 == nullptr);
    EXPECT_TRUE(own.get() != nullptr);

    EXPECT_FALSE(b);
    own = nullptr;
    EXPECT_TRUE(b);
}

KJ_TEST("Rc disown / reown") {
  bool b = false;
  SetTrueInDestructor* ptr = nullptr;

  {
    Rc<SetTrueInDestructor> ref = kj::rc<SetTrueInDestructor>(&b);
    ptr = ref.disown();
  }

  KJ_EXPECT(b == false);

  {
    auto ref = kj::Rc<SetTrueInDestructor>::reown(ptr);
  }

  KJ_EXPECT(b == true);
}

KJ_TEST("Rc cannot disown a projection") {
  bool destroyed = false;
  struct ProjectionOwner {
    explicit ProjectionOwner(bool* destroyed)
        : child(kj::rc<SetTrueInDestructor>(destroyed)) {}
    Rc<SetTrueInDestructor> child;
  };

  auto owner = kj::rc<ProjectionOwner>(&destroyed);
  auto projected = owner.addRef().project([](ProjectionOwner& owner) -> SetTrueInDestructor& {
    return *owner.child;
  });

  KJ_EXPECT_THROW_MESSAGE("cannot disown a projected Rc", projected.disown());
  KJ_EXPECT(projected != nullptr);
  owner = nullptr;
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Rc wraps Own of refcounted types") {
  bool b = false;

  Own<SetTrueInDestructor> own = kj::refcounted<SetTrueInDestructor>(&b);

  Rc<SetTrueInDestructor> ref(kj::mv(own));
  EXPECT_TRUE(own.get() == nullptr);
  EXPECT_TRUE(ref != nullptr);

  Rc<SetTrueInDestructor> ref2 = ref.addRef();
  EXPECT_TRUE(ref.get() == ref2.get());

  ref = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

struct SetTrueInDestructor2 {
  // Like SetTrueInDestructor but doesn't inherit Refcounted.

  SetTrueInDestructor2(bool* ptr): ptr(ptr) {}
  ~SetTrueInDestructor2() { *ptr = true; }

  bool* ptr;
};

KJ_TEST("Rc wraps non-refcounted types") {
  bool b = false;

  Rc<SetTrueInDestructor2> ref1 = kj::rc<SetTrueInDestructor2>(&b);
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(ref1 == nullptr);
  EXPECT_TRUE(&*ref1 == ref1.get());
  const auto& cref1 = ref1;
  EXPECT_TRUE(&*cref1 == ref1.get());

  Rc<SetTrueInDestructor2> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1 == ref2);
  EXPECT_TRUE(ref1.get() == ref2.get());

  EXPECT_FALSE(b);
  Own<SetTrueInDestructor2> own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() == ref2.get());
  EXPECT_FALSE(b);
  own = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Rc wraps Own of non-refcounted types") {
  bool b = false;

  Rc<SetTrueInDestructor2> ref1(kj::heap<SetTrueInDestructor2>(&b));
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(b);

  Rc<SetTrueInDestructor2> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1 == ref2);
  EXPECT_TRUE(ref1.get() == ref2.get());

  Own<SetTrueInDestructor2> own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() == ref2.get());

  own = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Rc wraps attached Own") {
  bool b = false;
  bool attached = false;

  Own<SetTrueInDestructor2> own = kj::heap<SetTrueInDestructor2>(&b)
      .attach(kj::heap<SetTrueInDestructor2>(&attached));
  Rc<SetTrueInDestructor2> ref(kj::mv(own));
  EXPECT_TRUE(own.get() == nullptr);
  EXPECT_TRUE(ref != nullptr);
  EXPECT_FALSE(b);
  EXPECT_FALSE(attached);

  Rc<SetTrueInDestructor2> ref2 = ref.addRef();

  ref = nullptr;
  EXPECT_FALSE(b);
  EXPECT_FALSE(attached);

  ref2 = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(attached);
}

KJ_TEST("Rc<String>") {
  Rc<String> ref1 = kj::rc<String>(kj::str("hello"));
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_TRUE(ref1->asPtr() == "hello");

  Rc<String> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1 == ref2);
  EXPECT_TRUE(ref1.get() == ref2.get());

  (*ref2)[0] = 'H';
  EXPECT_TRUE(ref1->asPtr() == "Hello");

  Own<String> own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() == ref2.get());
  EXPECT_TRUE(own->asPtr() == "Hello");

  own = nullptr;
  EXPECT_TRUE(ref2->asPtr() == "Hello");
}

struct Abstract {
  virtual ~Abstract() noexcept(false) = default;
  virtual void use() = 0;
};

struct Concrete final: public Abstract {
  Concrete(bool* ptr): ptr(ptr) {}
  ~Concrete() { *ptr = true; }
  void use() override {}
  bool* ptr;
};

KJ_TEST("Rc<Abstract>") {
  bool b = false;

  Rc<Abstract> ref(kj::heap<Concrete>(&b));
  EXPECT_TRUE(ref != nullptr);
  EXPECT_FALSE(b);
  EXPECT_TRUE(&*ref == ref.get());
  const auto& cref = ref;
  EXPECT_TRUE(&*cref == ref.get());

  ref->use();

  auto ref2 = ref.addRef();
  EXPECT_TRUE(ref == ref2);
  EXPECT_TRUE(ref.get() == ref2.get());

  Own<Abstract> own2 = ref.toOwn();
  EXPECT_TRUE(ref == nullptr);
  own2->use();
  EXPECT_FALSE(b);
  own2 = nullptr;
  EXPECT_FALSE(b);
  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Rc<Concrete>") {
  bool b = false;

  Rc<Concrete> ref = kj::rc<Concrete>(&b);
  EXPECT_TRUE(ref != nullptr);
  EXPECT_FALSE(b);
  EXPECT_TRUE(&*ref == ref.get());
  const auto& cref = ref;
  EXPECT_TRUE(&*cref == ref.get());

  auto own = ref.toOwn();
  EXPECT_TRUE(ref == nullptr);
  EXPECT_TRUE(own.get() != nullptr);
  EXPECT_FALSE(b);
  own = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Rc polymorphic upcast") {
  bool b = false;

  Rc<Concrete> ref = kj::rc<Concrete>(&b);
  Rc<Concrete> ref2 = ref.addRef();

  Rc<Abstract> abstract = kj::mv(ref);
  EXPECT_TRUE(ref == nullptr);
  EXPECT_TRUE(abstract.get() == ref2.get());

  abstract->use();
  EXPECT_FALSE(b);

  abstract = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

struct Child: public SetTrueInDestructor {
  Child(bool* ptr): SetTrueInDestructor(ptr) {}
};

KJ_TEST("Rc inheritance") {
  bool b = false;

  auto child = kj::rc<Child>(&b);

  // up casting works automatically
  kj::Rc<SetTrueInDestructor> parent = child.addRef();

  auto down = parent.downcast<Child>();
  EXPECT_TRUE(parent == nullptr);
  EXPECT_TRUE(down != nullptr);

  EXPECT_FALSE(b);
  child = nullptr;
  EXPECT_FALSE(b);
  down = nullptr;
  EXPECT_TRUE(b);
}

// Maybe<Rc<T>> is niche-optimized: a null Rc is the "none" state, so no extra flag is stored.
static_assert(NicheOptimizable<Rc<SetTrueInDestructor>>);
static_assert(sizeof(Maybe<Rc<SetTrueInDestructor>>) == sizeof(Rc<SetTrueInDestructor>));
static_assert(sizeof(Maybe<Rc<IncompleteDeclaredRefcounted>>) == 2 * sizeof(void*));
static_assert(sizeof(Maybe<Rc<IncompleteDeclaredNotRefcounted>>) == 2 * sizeof(void*));

KJ_TEST("Maybe<Rc<T>> niche optimization") {
  bool b = false;

  {
    Maybe<Rc<SetTrueInDestructor>> maybe;
    KJ_EXPECT(maybe == kj::none);

    maybe = kj::rc<SetTrueInDestructor>(&b);
    KJ_EXPECT(maybe != kj::none);
    KJ_IF_SOME(ref, maybe) {
      KJ_EXPECT(ref.get() != nullptr);
      KJ_EXPECT(ref->ptr == &b);
    } else {
      KJ_FAIL_EXPECT("expected value");
    }

    // Moving out leaves the source in the none state.
    Maybe<Rc<SetTrueInDestructor>> moved = kj::mv(maybe);
    KJ_EXPECT(maybe == kj::none);
    KJ_EXPECT(moved != kj::none);
    KJ_EXPECT(!b);

    // Move-assignment.
    maybe = kj::mv(moved);
    KJ_EXPECT(moved == kj::none);
    KJ_EXPECT(maybe != kj::none);
    KJ_EXPECT(!b);

    // Setting to none releases the reference.
    maybe = kj::none;
    KJ_EXPECT(maybe == kj::none);
    KJ_EXPECT(b);
  }

  {
    // Storing a null Rc yields none, consistent with the niche representation.
    Maybe<Rc<SetTrueInDestructor>> maybe = Rc<SetTrueInDestructor>();
    KJ_EXPECT(maybe == kj::none);
    maybe = Rc<SetTrueInDestructor>(nullptr);
    KJ_EXPECT(maybe == kj::none);
  }

  {
    // emplace()
    b = false;
    Maybe<Rc<SetTrueInDestructor>> maybe;
    auto& ref = maybe.emplace(kj::rc<SetTrueInDestructor>(&b));
    KJ_EXPECT(ref->ptr == &b);
    KJ_EXPECT(maybe != kj::none);
    KJ_EXPECT(!b);

    // Emplacing over an existing value releases the old one.
    bool b2 = false;
    maybe.emplace(kj::rc<SetTrueInDestructor>(&b2));
    KJ_EXPECT(b);
    KJ_EXPECT(!b2);
    maybe = kj::none;
    KJ_EXPECT(b2);
  }

  {
    // Destructor releases the reference.
    b = false;
    {
      Maybe<Rc<SetTrueInDestructor>> maybe = kj::rc<SetTrueInDestructor>(&b);
      KJ_EXPECT(!b);
    }
    KJ_EXPECT(b);
  }
}

KJ_TEST("Maybe<Rc<T>> converting constructor from Rc<Derived>") {
  bool b = false;

  auto child = kj::rc<Child>(&b);

  // Implicit conversion Rc<Child> -> Maybe<Rc<SetTrueInDestructor>> via copy-initialization.
  Maybe<Rc<SetTrueInDestructor>> maybe = child.addRef();
  KJ_EXPECT(maybe != kj::none);
  KJ_IF_SOME(ref, maybe) {
    KJ_EXPECT(ref.get() == child.get());
  }

  // Converting assignment.
  Maybe<Rc<SetTrueInDestructor>> maybe2;
  maybe2 = child.addRef();
  KJ_EXPECT(maybe2 != kj::none);

  // Maybe<Rc<Child>> -> Maybe<Rc<SetTrueInDestructor>>.
  Maybe<Rc<Child>> maybeChild = child.addRef();
  Maybe<Rc<SetTrueInDestructor>> maybe3 = kj::mv(maybeChild);
  KJ_EXPECT(maybeChild == kj::none);
  KJ_EXPECT(maybe3 != kj::none);

  child = nullptr;
  KJ_EXPECT(!b);
  maybe = kj::none;
  maybe2 = kj::none;
  KJ_EXPECT(!b);
  maybe3 = kj::none;
  KJ_EXPECT(b);
}

// Maybe<Rc<T>> does not implicitly convert to a reference to the referent.
static_assert(!canConvert<Maybe<Rc<SetTrueInDestructor>>&, Maybe<SetTrueInDestructor&>>());
static_assert(!canConvert<Maybe<Rc<SetTrueInDestructor>>&, Maybe<const SetTrueInDestructor&>>());
static_assert(!canConvert<const Maybe<Rc<SetTrueInDestructor>>&, Maybe<const SetTrueInDestructor&>>());
static_assert(!canConvert<Maybe<Rc<Child>>&, Maybe<SetTrueInDestructor&>>());

KJ_TEST("Maybe<Rc<T>> clone") {
  bool b = false;

  {
    Maybe<Rc<SetTrueInDestructor>> maybe = kj::rc<SetTrueInDestructor>(&b);
    Maybe<Rc<SetTrueInDestructor>> clone = maybe.clone();
    KJ_EXPECT(clone != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(maybe) == KJ_ASSERT_NONNULL(clone));

    maybe = kj::none;
    KJ_EXPECT(!b);
    clone = kj::none;
    KJ_EXPECT(b);

    Maybe<Rc<SetTrueInDestructor>> empty;
    KJ_EXPECT(empty.clone() == kj::none);
  }
}

static_assert(sizeof(WeakRc<SetTrueInDestructor>) == 2 * sizeof(void*));
static_assert(sizeof(WeakRc<IncompleteDeclaredRefcounted>) == 2 * sizeof(void*));
static_assert(sizeof(WeakRc<IncompleteDeclaredNotRefcounted>) == 2 * sizeof(void*));

static_assert(kj::canConvert<WeakRc<Child>, WeakRc<SetTrueInDestructor>>());
static_assert(!kj::canConvert<WeakRc<SetTrueInDestructor>, WeakRc<Child>>());

// WeakRc<T> is move-only; explicit copies are made via clone().
static_assert(Cloneable<WeakRc<SetTrueInDestructor>>);
static_assert(!Cloneable<const WeakRc<SetTrueInDestructor>>);

KJ_TEST("WeakRc basic") {
  bool b = false;
  Rc<SetTrueInDestructor> ref = kj::rc<SetTrueInDestructor>(&b);

  WeakRc<SetTrueInDestructor> weak = ref.downgrade();
  EXPECT_TRUE(weak != nullptr);
  EXPECT_FALSE(weak == nullptr);
  EXPECT_TRUE(weak == ref);
  EXPECT_TRUE(&weak.assertLive() == ref.get());

  // A WeakRc does not keep the referent alive on its own.
  EXPECT_FALSE(ref->isShared());

  KJ_IF_SOME(strong, weak.upgrade()) {
    static_assert(kj::isSameType<decltype(strong), kj::Rc<SetTrueInDestructor>&>());
    EXPECT_TRUE(strong == ref);
    // The upgraded strong reference holds the refcount while it lives.
    EXPECT_TRUE(ref->isShared());
  } else {
    KJ_FAIL_EXPECT("expected WeakRc to upgrade while referent is alive");
  }
  EXPECT_FALSE(ref->isShared());
  EXPECT_FALSE(b);

  ref = nullptr;
  EXPECT_TRUE(b);

  // The WeakRc has now expired.
  EXPECT_TRUE(weak == nullptr);
  EXPECT_TRUE(weak.tryGet() == kj::none);
  EXPECT_TRUE(weak.upgrade() == kj::none);
  EXPECT_TRUE(weak == ref); // both are null
#ifdef KJ_DEBUG
  KJ_EXPECT_THROW_MESSAGE("null WeakRc<> dereference", (void)weak.assertLive());
#endif
}

KJ_TEST("WeakRc KJ_IF_SOME and tryGet") {
  bool b = false;
  auto ref = kj::rc<SetTrueInDestructor>(&b);
  auto weak = ref.downgrade();

  KJ_IF_SOME(strong, weak) {
    static_assert(kj::isSameType<decltype(strong), kj::Rc<SetTrueInDestructor>&>());
    EXPECT_TRUE(strong == ref);
  } else {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on WeakRc to upgrade");
  }

  KJ_IF_SOME(obj, weak.tryGet()) {
    static_assert(kj::isSameType<decltype(obj), SetTrueInDestructor&>());
    EXPECT_TRUE(&obj == ref.get());
  } else {
    KJ_FAIL_EXPECT("expected tryGet to succeed");
  }
}

KJ_TEST("WeakRc KJ_REQUIRE_NONNULL") {
  bool b = false;
  auto ref = kj::rc<SetTrueInDestructor>(&b);
  auto weak = ref.downgrade();

  {
    kj::Rc<SetTrueInDestructor> strong = KJ_REQUIRE_NONNULL(weak);
    EXPECT_TRUE(strong == ref);
  }

  ref = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak == nullptr);

#if defined(KJ_ENABLE_IREQUIRE) && KJ_ENABLE_IREQUIRE
  KJ_EXPECT_THROW_MESSAGE("weak != nullptr", (void)KJ_REQUIRE_NONNULL(weak));
#endif
}

KJ_TEST("WeakRc const readMaybe integration") {
  bool b = false;
  auto ref = kj::rc<SetTrueInDestructor>(&b);

  // readMaybe (and thus KJ_IF_SOME / KJ_REQUIRE_NONNULL) must work on a const WeakRc<T> even
  // though T itself is non-const.
  const WeakRc<SetTrueInDestructor> weak = ref.downgrade();

  KJ_IF_SOME(strong, weak) {
    static_assert(kj::isSameType<decltype(strong), kj::Rc<SetTrueInDestructor>&>());
    EXPECT_TRUE(strong == ref);
  } else {
    KJ_FAIL_EXPECT("expected KJ_IF_SOME on const WeakRc to upgrade");
  }

  {
    kj::Rc<SetTrueInDestructor> strong = KJ_REQUIRE_NONNULL(weak);
    EXPECT_TRUE(strong == ref);
  }
}

KJ_TEST("WeakRc expires when Rc dropped, observed through Maybe") {
  bool b = false;
  kj::Maybe<WeakRc<SetTrueInDestructor>> maybeWeak;
  {
    auto ref = kj::rc<SetTrueInDestructor>(&b);
    maybeWeak = ref.downgrade();

    KJ_IF_SOME(weak, maybeWeak) {
      EXPECT_TRUE(&weak.assertLive() == ref.get());
    } else {
      KJ_FAIL_EXPECT("expected Maybe<WeakRc<T>> to contain a value");
    }
    EXPECT_FALSE(b);
  }
  EXPECT_TRUE(b);

  KJ_IF_SOME(weak, maybeWeak) {
    EXPECT_TRUE(weak.tryGet() == kj::none);
    EXPECT_TRUE(weak.upgrade() == kj::none);
    KJ_IF_SOME(obj, weak) {
      KJ_FAIL_EXPECT("expected KJ_IF_SOME on expired WeakRc<T> to be empty", obj.get());
    } else {
      EXPECT_TRUE(true);
    }
  } else {
    KJ_FAIL_EXPECT("expected Maybe<WeakRc<T>> to still contain the (expired) value");
  }
}

KJ_TEST("WeakRc upgrade extends lifetime") {
  bool b = false;
  WeakRc<SetTrueInDestructor> weak = nullptr;
  kj::Maybe<kj::Rc<SetTrueInDestructor>> strong;
  {
    auto ref = kj::rc<SetTrueInDestructor>(&b);
    weak = ref.downgrade();
    KJ_IF_SOME(s, weak.upgrade()) {
      strong = kj::mv(s);
    } else {
      KJ_FAIL_EXPECT("expected WeakRc to upgrade");
    }
  }

  // The original Rc is gone, but the upgraded Rc keeps the object alive.
  EXPECT_FALSE(b);
  EXPECT_TRUE(weak != nullptr);

  strong = kj::none;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak == nullptr);
}

KJ_TEST("WeakRc clone and move") {
  bool b = false;
  auto ref = kj::rc<SetTrueInDestructor>(&b);

  WeakRc<SetTrueInDestructor> weak1 = ref.downgrade();
  WeakRc<SetTrueInDestructor> weak2 = weak1.clone();  // explicit copy
  EXPECT_TRUE(weak1 == weak2);
  EXPECT_TRUE(weak1 == ref);

  WeakRc<SetTrueInDestructor> weak3 = kj::mv(weak1);  // move ctor
  EXPECT_TRUE(weak1 == nullptr);
  EXPECT_TRUE(weak3 == ref);

  WeakRc<SetTrueInDestructor> weak4 = nullptr;
  weak4 = weak3.clone();  // explicit copy + move assign
  EXPECT_TRUE(weak4 == ref);

  WeakRc<SetTrueInDestructor> weak5 = nullptr;
  weak5 = kj::mv(weak4);  // move assign
  EXPECT_TRUE(weak4 == nullptr);
  EXPECT_TRUE(weak5 == ref);

  weak2 = nullptr;
  EXPECT_TRUE(weak2 == nullptr);

  EXPECT_FALSE(b);
  ref = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("WeakRc self-assignment") {
  bool b = false;
  auto ref = kj::rc<SetTrueInDestructor>(&b);

  WeakRc<SetTrueInDestructor> weak = ref.downgrade();
  auto ptr = &weak.assertLive();
  auto weakPtr = &weak;

  weak = kj::mv(*weakPtr);

  EXPECT_TRUE(weak != nullptr);
  EXPECT_TRUE(weak == ref);
  EXPECT_TRUE(&weak.assertLive() == ptr);
  EXPECT_FALSE(b);

  ref = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak == nullptr);
}

KJ_TEST("WeakRc subtyping and construction from Rc") {
  bool b = false;
  auto child = kj::rc<Child>(&b);

  WeakRc<Child> weakChild = child.downgrade();
  WeakRc<SetTrueInDestructor> weakParent = weakChild.clone();  // upcast (clone + move)
  EXPECT_TRUE(weakParent == child);
  EXPECT_TRUE(&weakParent.assertLive() == child.get());

  // Construct WeakRc<Base> directly from Rc<Derived>.
  WeakRc<SetTrueInDestructor> weakParent2 = child;
  EXPECT_TRUE(weakParent2 == child);

  // Maybe<WeakRc<T>> holding a moved weak reference.
  kj::Maybe<WeakRc<Child>> maybeWeak = weakChild.clone();
  KJ_IF_SOME(strong, maybeWeak) {
    EXPECT_TRUE(strong == child);
  } else {
    KJ_FAIL_EXPECT("expected Maybe<WeakRc<T>> to upgrade");
  }

  child = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weakParent == nullptr);
  EXPECT_TRUE(weakParent2 == nullptr);
}

KJ_TEST("WeakRc polymorphic upcast") {
  bool b = false;
  Rc<Concrete> ref = kj::rc<Concrete>(&b);

  WeakRc<Abstract> weak = ref.downgrade();  // Rc<Concrete> -> WeakRc<Abstract>
  EXPECT_TRUE(weak != nullptr);

  KJ_IF_SOME(strong, weak.upgrade()) {
    strong->use();
  } else {
    KJ_FAIL_EXPECT("expected WeakRc<Abstract> to upgrade");
  }

  EXPECT_FALSE(b);
  ref = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak == nullptr);
}

KJ_TEST("WeakRc with non-refcounted type") {
  bool b = false;
  Rc<SetTrueInDestructor2> ref = kj::rc<SetTrueInDestructor2>(&b);

  WeakRc<SetTrueInDestructor2> weak = ref.downgrade();
  EXPECT_TRUE(weak == ref);

  auto ref2 = ref.addRef();
  ref = nullptr;
  EXPECT_FALSE(b);
  EXPECT_TRUE(weak != nullptr);

  KJ_IF_SOME(strong, weak.upgrade()) {
    EXPECT_TRUE(strong.get() == ref2.get());
  } else {
    KJ_FAIL_EXPECT("expected WeakRc to upgrade while a strong ref exists");
  }

  ref2 = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak == nullptr);
}

KJ_TEST("WeakRc from null Rc") {
  Rc<SetTrueInDestructor> ref = nullptr;
  WeakRc<SetTrueInDestructor> weak = ref.downgrade();
  EXPECT_TRUE(weak == nullptr);
  EXPECT_TRUE(weak.tryGet() == kj::none);
  EXPECT_TRUE(weak.upgrade() == kj::none);
}

KJ_TEST("WeakRc addWeakRef/addStrongRef synonyms") {
  bool b = false;
  auto ref = kj::rc<SetTrueInDestructor>(&b);

  WeakRc<SetTrueInDestructor> weak = ref.addWeakRef();
  EXPECT_TRUE(weak == ref);

  KJ_IF_SOME(strong, weak.addStrongRef()) {
    EXPECT_TRUE(strong == ref);
  } else {
    KJ_FAIL_EXPECT("expected addStrongRef() to upgrade while referent is alive");
  }

  ref = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak.addStrongRef() == kj::none);
}

KJ_TEST("Refcounted::addRefToThis") {
  bool b = false;

  auto ref1 = kj::rc<SetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());

  auto ref2 = ref1->newRef();
  EXPECT_TRUE(ref2->isShared());
  EXPECT_TRUE(ref1->isShared());
  EXPECT_FALSE(b);

  ref1 = nullptr;
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Refcounted::addWeakToThis") {
  bool b = false;

  auto ref = kj::rc<SetTrueInDestructor>(&b);
  WeakRc<SetTrueInDestructor> weak = ref->newWeakRef();
  EXPECT_TRUE(weak == ref);

  // A weak reference created from `this` does not keep the object alive.
  EXPECT_FALSE(ref->isShared());

  KJ_IF_SOME(strong, weak.upgrade()) {
    EXPECT_TRUE(strong == ref);
    EXPECT_TRUE(ref->isShared());
  } else {
    KJ_FAIL_EXPECT("expected WeakRc to upgrade while referent is alive");
  }

  EXPECT_FALSE(ref->isShared());
  ref = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak == nullptr);
}

KJ_TEST("Refcounted::addWeakToThis in constructor") {
  bool b = false;

  auto ref = kj::rc<WeakInConstructor>(&b);

  // The weak reference captured during construction is valid and refers to the object.
  EXPECT_TRUE(ref->weak == ref);
  EXPECT_FALSE(ref->isShared());

  KJ_IF_SOME(strong, ref->weak.upgrade()) {
    EXPECT_TRUE(strong == ref);
  } else {
    KJ_FAIL_EXPECT("expected WeakRc captured in constructor to upgrade while referent is alive");
  }

  // Grab an independent weak reference before dropping the object; it must observe expiration once
  // the last strong reference is gone.
  WeakRc<WeakInConstructor> weak = ref->weak.clone();

  ref = nullptr;
  EXPECT_TRUE(b);
  EXPECT_TRUE(weak == nullptr);
}

KJ_TEST("RefcountedWrapper") {
  {
    bool b = false;
    Own<RefcountedWrapper<SetTrueInDestructor2>> w = refcountedWrapper<SetTrueInDestructor2>(&b);
    KJ_EXPECT(!b);

    Own<SetTrueInDestructor2> ref1 = w->addWrappedRef();
    Own<SetTrueInDestructor2> ref2 = w->addWrappedRef();

    KJ_EXPECT(ref1.get() == &w->getWrapped());
    KJ_EXPECT(ref1.get() == ref2.get());

    KJ_EXPECT(!b);

    w = nullptr;
    ref1 = nullptr;

    KJ_EXPECT(!b);

    ref2 = nullptr;

    KJ_EXPECT(b);
  }

  // Wrap Own<T>.
  {
    bool b = false;
    Own<RefcountedWrapper<Own<SetTrueInDestructor2>>> w =
        refcountedWrapper<SetTrueInDestructor2>(kj::heap<SetTrueInDestructor2>(&b));
    KJ_EXPECT(!b);

    Own<SetTrueInDestructor2> ref1 = w->addWrappedRef();
    Own<SetTrueInDestructor2> ref2 = w->addWrappedRef();

    KJ_EXPECT(ref1.get() == &w->getWrapped());
    KJ_EXPECT(ref1.get() == ref2.get());

    KJ_EXPECT(!b);

    w = nullptr;
    ref1 = nullptr;

    KJ_EXPECT(!b);

    ref2 = nullptr;

    KJ_EXPECT(b);
  }

  // Try wrapping an `int` to really demonstrate the wrapped type can be anything.
  {
    Own<RefcountedWrapper<int>> w = refcountedWrapper<int>(123);
    int* ptr = &w->getWrapped();
    KJ_EXPECT(*ptr == 123);

    Own<int> ref1 = w->addWrappedRef();
    Own<int> ref2 = w->addWrappedRef();

    KJ_EXPECT(ref1.get() == ptr);
    KJ_EXPECT(ref2.get() == ptr);

    w = nullptr;
    ref1 = nullptr;

    KJ_EXPECT(*ref2 == 123);
  }
}


struct AtomicSetTrueInDestructor: public AtomicRefcounted {

  AtomicSetTrueInDestructor(bool* ptr): ptr(ptr) {}
  ~AtomicSetTrueInDestructor() { *ptr = true; }

  kj::Arc<AtomicSetTrueInDestructor> newRef() const { return addRefToThis(); }

  bool* ptr;
};

static_assert(Cloneable<Arc<AtomicSetTrueInDestructor>>);
static_assert(Cloneable<const Arc<AtomicSetTrueInDestructor>>);

struct IncompleteDeclaredAtomicRefcounted;
static_assert(sizeof(Arc<IncompleteDeclaredAtomicRefcounted>) == 2 * sizeof(void*));

struct IncompleteDeclaredAtomicRefcounted: public AtomicRefcounted {
  IncompleteDeclaredAtomicRefcounted(bool* ptr): ptr(ptr) {}
  ~IncompleteDeclaredAtomicRefcounted() { *ptr = true; }

  bool* ptr;
};

struct IncompleteDeclaredNotAtomicRefcounted;
static_assert(sizeof(Arc<IncompleteDeclaredNotAtomicRefcounted>) == 2 * sizeof(void*));

struct IncompleteDeclaredNotAtomicRefcounted {
  IncompleteDeclaredNotAtomicRefcounted(bool* ptr): ptr(ptr) {}
  ~IncompleteDeclaredNotAtomicRefcounted() { *ptr = true; }

  bool* ptr;
};

struct IncompleteInnerDeclaredAtomicRefcounted {
private:
  struct Inner;
  static_assert(sizeof(Arc<Inner>) == 2 * sizeof(void*));

public:
  static void test();
};

struct IncompleteInnerDeclaredAtomicRefcounted::Inner: public AtomicRefcounted {
  Inner(bool* ptr): ptr(ptr) {}
  ~Inner() { *ptr = true; }

  bool* ptr;
};

void IncompleteInnerDeclaredAtomicRefcounted::test() {
  bool b = false;
  Arc<Inner> ref = kj::arc<Inner>(&b);
  KJ_EXPECT(!b);
  ref = nullptr;
  KJ_EXPECT(b);
}

struct IncompleteInnerDeclaredNotAtomicRefcounted {
private:
  struct Inner;
  static_assert(sizeof(Arc<Inner>) == 2 * sizeof(void*));

public:
  static void test();
};

struct IncompleteInnerDeclaredNotAtomicRefcounted::Inner {
  Inner(bool* ptr): ptr(ptr) {}
  ~Inner() { *ptr = true; }

  bool* ptr;
};

void IncompleteInnerDeclaredNotAtomicRefcounted::test() {
  bool b = false;
  Arc<Inner> ref = kj::arc<Inner>(&b);
  KJ_EXPECT(!b);
  auto ref2 = ref.addRef();
  ref = nullptr;
  KJ_EXPECT(!b);
  ref2 = nullptr;
  KJ_EXPECT(b);
}

KJ_TEST("Arc incomplete declared atomic refcounted types") {
  {
    bool b = false;
    Arc<IncompleteDeclaredAtomicRefcounted> ref =
        kj::arc<IncompleteDeclaredAtomicRefcounted>(&b);
    KJ_EXPECT(!b);
    ref = nullptr;
    KJ_EXPECT(b);
  }

  IncompleteInnerDeclaredAtomicRefcounted::test();
}

KJ_TEST("Arc incomplete declared non-atomic-refcounted types") {
  {
    bool b = false;
    Arc<IncompleteDeclaredNotAtomicRefcounted> ref =
        kj::arc<IncompleteDeclaredNotAtomicRefcounted>(&b);
    KJ_EXPECT(!b);
    auto ref2 = ref.addRef();
    ref = nullptr;
    KJ_EXPECT(!b);
    ref2 = nullptr;
    KJ_EXPECT(b);
  }

  IncompleteInnerDeclaredNotAtomicRefcounted::test();
}

KJ_TEST("Arc") {
  bool b = false;

  kj::Arc<AtomicSetTrueInDestructor> ref1 = kj::arc<AtomicSetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());
  EXPECT_TRUE(&*ref1 == ref1.get());
  const auto& cref1 = ref1;
  EXPECT_TRUE(&*cref1 == ref1.get());
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(ref1 == nullptr);

  kj::Arc<AtomicSetTrueInDestructor> ref2 = ref1.addRef();

  // can be always cast to Arc<const T>
  kj::Arc<const AtomicSetTrueInDestructor> ref3 = ref1.addRef();

  // addRef works for const references too
  kj::Arc<const AtomicSetTrueInDestructor> ref4 = ref3.addRef();

  ref1 = nullptr;
  EXPECT_TRUE(ref1 == nullptr);
  ref2 = nullptr;
  EXPECT_TRUE(ref2 == nullptr);
  ref3 = nullptr;
  EXPECT_TRUE(ref3 == nullptr);

  EXPECT_FALSE(b);
  ref4 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc clone") {
  bool b = false;

  auto ref1 = kj::arc<AtomicSetTrueInDestructor>(&b);
  const auto& cref = ref1;
  auto ref2 = cref.clone();

  EXPECT_TRUE(ref1 == ref2);

  ref1 = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc project retains ownership of the original object") {
  bool destroyed = false;
  auto ref = kj::arc<ProjectionTarget>(&destroyed, 123);
  auto other = ref.addRef();
  const int* value = &ref->value;
  auto projectionSource = ref.addRef();

  Arc<const int> projected = kj::mv(projectionSource).project(
      [](const ProjectionTarget& target) -> const int& { return target.value; });

  KJ_EXPECT(projectionSource == nullptr);
  KJ_EXPECT(ref != nullptr);
  KJ_EXPECT(projected.get() == value);
  KJ_EXPECT(*projected == 123);
  KJ_EXPECT(ref->value == 123);
  KJ_EXPECT(other->value == 123);

  other = nullptr;
  ref = nullptr;
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);

#if defined(KJ_ENABLE_IREQUIRE) && KJ_ENABLE_IREQUIRE
  Arc<ProjectionTarget> nullRef;
  bool called = false;
  KJ_EXPECT_THROW_MESSAGE("null Arc<> projection",
      nullRef.addRef().project([&](const ProjectionTarget& target) -> const int& {
    called = true;
    return target.value;
  }));
  KJ_EXPECT(!called);
#endif
}

KJ_TEST("Arc project retains ownership while invoking callback") {
  bool destroyed = false;
  auto foo = kj::arc<ProjectionTarget>(&destroyed, 123);

  Arc<const int> projected = foo.addRef().project(
      [&](const ProjectionTarget& f) -> const int& {
    foo = nullptr;
    KJ_EXPECT(!destroyed);
    return f.value;
  });

  KJ_EXPECT(foo == nullptr);
  KJ_EXPECT(*projected == 123);
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Arc project retains ownership when callback nullifies source") {
  bool destroyed = false;
  auto foo = kj::arc<ProjectionTarget>(&destroyed, 123);

  Arc<const int> projected = kj::mv(foo).project(
      [&](const ProjectionTarget& f) -> const int& {
    KJ_EXPECT(foo == nullptr);
    foo = nullptr;
    KJ_REQUIRE(!destroyed);
    return f.value;
  });

  KJ_EXPECT(foo == nullptr);
  KJ_EXPECT(*projected == 123);
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Arc project supports identity") {
  bool destroyed = false;
  auto ref = kj::arc<ProjectionTarget>(&destroyed, 123);
  auto original = ref.get();

  auto projected = kj::mv(ref).project(
      [](const ProjectionTarget& target) -> const ProjectionTarget& { return target; });

  KJ_EXPECT(ref == nullptr);
  KJ_EXPECT(projected.get() == original);
  KJ_EXPECT(projected->value == 123);
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Arc supports projections deeper than one level") {
  struct Inner { int value = 123; };
  struct Middle { Inner inner; };
  struct Outer { int prefix = 0; Middle middle; };

  auto outer = kj::arc<Outer>();
  auto middle = outer.addRef().project(
      [](const Outer& outer) -> const Middle& { return outer.middle; });
  auto inner = middle.addRef().project(
      [](const Middle& middle) -> const Inner& { return middle.inner; });
  auto value = inner.addRef().project([](const Inner& inner) -> const int& { return inner.value; });

  KJ_EXPECT(*value == 123);
}

struct AtomicChild: public AtomicSetTrueInDestructor {
  AtomicChild(bool* ptr): AtomicSetTrueInDestructor(ptr) {}
};

KJ_TEST("Arc inheritance") {
  bool b = false;

  auto child = kj::arc<AtomicChild>(&b);

  // up casting works automatically
  kj::Arc<AtomicSetTrueInDestructor> parent = child.addRef();

  auto down = parent.downcast<AtomicChild>();
  EXPECT_TRUE(parent == nullptr);
  EXPECT_TRUE(down != nullptr);

  EXPECT_FALSE(b);
  child = nullptr;
  EXPECT_FALSE(b);
  down = nullptr;
  EXPECT_TRUE(b);
}

// Maybe<Arc<T>> is niche-optimized: a null Arc is the "none" state, so no extra flag is stored.
static_assert(NicheOptimizable<Arc<AtomicSetTrueInDestructor>>);
static_assert(sizeof(Maybe<Arc<AtomicSetTrueInDestructor>>) == sizeof(Arc<AtomicSetTrueInDestructor>));
static_assert(sizeof(Maybe<Arc<IncompleteDeclaredAtomicRefcounted>>) == 2 * sizeof(void*));
static_assert(sizeof(Maybe<Arc<IncompleteDeclaredNotAtomicRefcounted>>) == 2 * sizeof(void*));

static_assert(Cloneable<Maybe<Arc<AtomicSetTrueInDestructor>>>);
static_assert(Cloneable<const Maybe<Arc<AtomicSetTrueInDestructor>>>);

// Maybe<Arc<T>> does not implicitly convert to a reference to the referent.
static_assert(!canConvert<Maybe<Arc<AtomicSetTrueInDestructor>>&,
                          Maybe<const AtomicSetTrueInDestructor&>>());
static_assert(!canConvert<Maybe<Arc<AtomicSetTrueInDestructor>>&,
                          Maybe<AtomicSetTrueInDestructor&>>());
static_assert(!canConvert<const Maybe<Arc<AtomicSetTrueInDestructor>>&,
                          Maybe<const AtomicSetTrueInDestructor&>>());
static_assert(!canConvert<Maybe<Arc<AtomicChild>>&, Maybe<const AtomicSetTrueInDestructor&>>());

KJ_TEST("Maybe<Arc<T>> niche optimization") {
  bool b = false;

  {
    Maybe<Arc<AtomicSetTrueInDestructor>> maybe;
    KJ_EXPECT(maybe == kj::none);

    maybe = kj::arc<AtomicSetTrueInDestructor>(&b);
    KJ_EXPECT(maybe != kj::none);
    KJ_IF_SOME(ref, maybe) {
      KJ_EXPECT(ref.get() != nullptr);
      KJ_EXPECT(ref->ptr == &b);
    } else {
      KJ_FAIL_EXPECT("expected value");
    }

    // Moving out leaves the source in the none state.
    Maybe<Arc<AtomicSetTrueInDestructor>> moved = kj::mv(maybe);
    KJ_EXPECT(maybe == kj::none);
    KJ_EXPECT(moved != kj::none);
    KJ_EXPECT(!b);

    // Move-assignment.
    maybe = kj::mv(moved);
    KJ_EXPECT(moved == kj::none);
    KJ_EXPECT(maybe != kj::none);
    KJ_EXPECT(!b);

    // Setting to none releases the reference.
    maybe = kj::none;
    KJ_EXPECT(maybe == kj::none);
    KJ_EXPECT(b);
  }

  {
    // Storing a null Arc yields none, consistent with the niche representation.
    Maybe<Arc<AtomicSetTrueInDestructor>> maybe = Arc<AtomicSetTrueInDestructor>();
    KJ_EXPECT(maybe == kj::none);
    maybe = Arc<AtomicSetTrueInDestructor>(nullptr);
    KJ_EXPECT(maybe == kj::none);
  }

  {
    // emplace()
    b = false;
    Maybe<Arc<AtomicSetTrueInDestructor>> maybe;
    auto& ref = maybe.emplace(kj::arc<AtomicSetTrueInDestructor>(&b));
    KJ_EXPECT(ref->ptr == &b);
    KJ_EXPECT(maybe != kj::none);
    KJ_EXPECT(!b);

    // Emplacing over an existing value releases the old one.
    bool b2 = false;
    maybe.emplace(kj::arc<AtomicSetTrueInDestructor>(&b2));
    KJ_EXPECT(b);
    KJ_EXPECT(!b2);
    maybe = kj::none;
    KJ_EXPECT(b2);
  }

  {
    // Destructor releases the reference.
    b = false;
    {
      Maybe<Arc<AtomicSetTrueInDestructor>> maybe = kj::arc<AtomicSetTrueInDestructor>(&b);
      KJ_EXPECT(!b);
    }
    KJ_EXPECT(b);
  }
}

KJ_TEST("Maybe<Arc<T>> converting constructor from Arc<Derived>") {
  bool b = false;

  auto child = kj::arc<AtomicChild>(&b);

  // Implicit conversion Arc<AtomicChild> -> Maybe<Arc<AtomicSetTrueInDestructor>> via
  // copy-initialization.
  Maybe<Arc<AtomicSetTrueInDestructor>> maybe = child.addRef();
  KJ_EXPECT(maybe != kj::none);
  KJ_IF_SOME(ref, maybe) {
    KJ_EXPECT(ref.get() == child.get());
  }

  // Converting assignment.
  Maybe<Arc<AtomicSetTrueInDestructor>> maybe2;
  maybe2 = child.addRef();
  KJ_EXPECT(maybe2 != kj::none);

  // Maybe<Arc<AtomicChild>> -> Maybe<Arc<AtomicSetTrueInDestructor>>.
  Maybe<Arc<AtomicChild>> maybeChild = child.addRef();
  Maybe<Arc<AtomicSetTrueInDestructor>> maybe3 = kj::mv(maybeChild);
  KJ_EXPECT(maybeChild == kj::none);
  KJ_EXPECT(maybe3 != kj::none);

  child = nullptr;
  KJ_EXPECT(!b);
  maybe = kj::none;
  maybe2 = kj::none;
  KJ_EXPECT(!b);
  maybe3 = kj::none;
  KJ_EXPECT(b);
}

KJ_TEST("Maybe<Arc<T>> clone") {
  bool b = false;

  {
    Maybe<Arc<AtomicSetTrueInDestructor>> maybe = kj::arc<AtomicSetTrueInDestructor>(&b);
    const auto& constMaybe = maybe;
    Maybe<Arc<AtomicSetTrueInDestructor>> clone = constMaybe.clone();
    KJ_EXPECT(clone != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(maybe) == KJ_ASSERT_NONNULL(clone));

    maybe = kj::none;
    KJ_EXPECT(!b);
    clone = kj::none;
    KJ_EXPECT(b);

    Maybe<Arc<AtomicSetTrueInDestructor>> empty;
    KJ_EXPECT(empty.clone() == kj::none);
  }
}

KJ_TEST("atomicAddRef is safe under concurrent reference-count changes") {
  bool destroyed = false;
  auto owner = kj::atomicRefcounted<AtomicSetTrueInDestructor>(&destroyed);
  auto* object = owner.get();
  std::atomic<bool> start(false);

  auto addRefs = [&]() noexcept {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (uint i = 0; i < 1000; ++i) {
      auto ref = kj::atomicAddRef(*object);
    }
  };

  kj::Thread thread1(addRefs);
  kj::Thread thread2(addRefs);
  start.store(true, std::memory_order_release);
}

KJ_TEST("Arc concurrent final release is synchronized") {
  bool destroyed = false;
  auto ref1 = kj::arc<AtomicSetTrueInDestructor>(&destroyed);
  auto ref2 = ref1.addRef();
  std::atomic<bool> start(false);

  kj::Thread thread1([ref = kj::mv(ref1), &start]() mutable noexcept {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    ref = nullptr;
  });
  kj::Thread thread2([ref = kj::mv(ref2), &start]() mutable noexcept {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    ref = nullptr;
  });

  start.store(true, std::memory_order_release);
}

KJ_TEST("AtomicRefcounted::addRefToThis") {
  bool b = false;

  kj::Arc<AtomicSetTrueInDestructor> ref1 = kj::arc<AtomicSetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());

  kj::Arc<AtomicSetTrueInDestructor> ref2 = ref1->newRef();
  EXPECT_TRUE(ref2->isShared());
  EXPECT_TRUE(ref1->isShared());
  EXPECT_FALSE(b);

  ref1 = nullptr;
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc Own interop") {
  bool b = false;

  kj::Arc<AtomicSetTrueInDestructor> ref1 = kj::arc<AtomicSetTrueInDestructor>(&b);

  EXPECT_FALSE(b);
  auto own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() != nullptr);

  EXPECT_FALSE(b);
  own = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc disown / reown") {
  bool b = false;
  const AtomicSetTrueInDestructor* ptr = nullptr;

  {
    kj::Arc<AtomicSetTrueInDestructor> ref = kj::arc<AtomicSetTrueInDestructor>(&b);
    ptr = ref.disown();
  }

  KJ_EXPECT(b == false);

  {
    auto ref = kj::Arc<AtomicSetTrueInDestructor>::reown(ptr);
  }

  KJ_EXPECT(b == true);
}

KJ_TEST("Arc cannot disown a projection") {
  bool destroyed = false;
  struct ProjectionOwner {
    explicit ProjectionOwner(bool* destroyed)
        : child(kj::arc<AtomicSetTrueInDestructor>(destroyed)) {}
    Arc<AtomicSetTrueInDestructor> child;
  };

  auto owner = kj::arc<ProjectionOwner>(&destroyed);
  auto projected = owner.addRef().project(
      [](const ProjectionOwner& owner) -> const AtomicSetTrueInDestructor& {
    return *owner.child;
  });

  KJ_EXPECT_THROW_MESSAGE("cannot disown a projected Arc", projected.disown());
  KJ_EXPECT(projected != nullptr);
  owner = nullptr;
  KJ_EXPECT(!destroyed);
  projected = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Arc wraps non-atomic-refcounted types") {
  bool b = false;

  Arc<SetTrueInDestructor2> ref1 = kj::arc<SetTrueInDestructor2>(&b);
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(ref1 == nullptr);
  EXPECT_TRUE(&*ref1 == ref1.get());
  const auto& cref1 = ref1;
  EXPECT_TRUE(&*cref1 == ref1.get());

  Arc<SetTrueInDestructor2> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1 == ref2);
  EXPECT_TRUE(ref1.get() == ref2.get());

  EXPECT_FALSE(b);
  Own<const SetTrueInDestructor2> own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() == ref2.get());
  EXPECT_FALSE(b);
  own = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc wraps Own of non-atomic-refcounted types") {
  bool b = false;

  Arc<SetTrueInDestructor2> ref1(kj::heap<SetTrueInDestructor2>(&b));
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_FALSE(b);

  Arc<SetTrueInDestructor2> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1 == ref2);
  EXPECT_TRUE(ref1.get() == ref2.get());

  Own<const SetTrueInDestructor2> own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() == ref2.get());

  own = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc<String>") {
  Arc<String> ref1 = kj::arc<String>(kj::str("hello"));
  EXPECT_TRUE(ref1 != nullptr);
  EXPECT_TRUE(ref1->asPtr() == "hello");

  Arc<String> ref2 = ref1.addRef();
  EXPECT_TRUE(ref1 == ref2);
  EXPECT_TRUE(ref1.get() == ref2.get());

  Own<const String> own = ref1.toOwn();
  EXPECT_TRUE(ref1 == nullptr);
  EXPECT_TRUE(own.get() == ref2.get());
  EXPECT_TRUE(own->asPtr() == "hello");

  own = nullptr;
  EXPECT_TRUE(ref2->asPtr() == "hello");
}

struct AbstractForArc {
  virtual ~AbstractForArc() noexcept(false) = default;
  virtual void use() const = 0;
};

struct ConcreteForArc final: public AbstractForArc {
  ConcreteForArc(bool* ptr): ptr(ptr) {}
  ~ConcreteForArc() { *ptr = true; }
  void use() const override {}
  bool* ptr;
};

struct AbstractAtomicRefcounted: public AtomicRefcounted {
  virtual void use() const = 0;
};

struct ConcreteAtomicRefcounted final: public AbstractForArc, public AbstractAtomicRefcounted {
  ConcreteAtomicRefcounted(bool* ptr): ptr(ptr) {}
  ~ConcreteAtomicRefcounted() { *ptr = true; }
  void use() const override {}

  bool* ptr;
};

KJ_TEST("Arc<Abstract>") {
  bool b = false;

  Arc<AbstractForArc> ref(kj::heap<ConcreteForArc>(&b));
  EXPECT_TRUE(ref != nullptr);
  EXPECT_FALSE(b);
  EXPECT_TRUE(&*ref == ref.get());
  const auto& cref = ref;
  EXPECT_TRUE(&*cref == ref.get());

  ref->use();

  auto ref2 = ref.addRef();
  EXPECT_TRUE(ref == ref2);
  EXPECT_TRUE(ref.get() == ref2.get());

  Own<const AbstractForArc> own2 = ref.toOwn();
  EXPECT_TRUE(ref == nullptr);
  own2->use();
  EXPECT_FALSE(b);
  own2 = nullptr;
  EXPECT_FALSE(b);
  ref2 = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc<Concrete>") {
  bool b = false;

  Arc<ConcreteForArc> ref = kj::arc<ConcreteForArc>(&b);
  EXPECT_TRUE(ref != nullptr);
  EXPECT_FALSE(b);
  EXPECT_TRUE(&*ref == ref.get());
  const auto& cref = ref;
  EXPECT_TRUE(&*cref == ref.get());

  auto own = ref.toOwn();
  EXPECT_TRUE(ref == nullptr);
  EXPECT_TRUE(own.get() != nullptr);
  EXPECT_FALSE(b);
  own = nullptr;
  EXPECT_TRUE(b);
}

KJ_TEST("Arc polymorphic upcast") {
  bool b = false;

  Arc<ConcreteForArc> ref = kj::arc<ConcreteForArc>(&b);
  Arc<ConcreteForArc> ref2 = ref.addRef();

  Arc<AbstractForArc> abstract = kj::mv(ref);
  EXPECT_TRUE(ref == nullptr);
  EXPECT_TRUE(abstract.get() == ref2.get());

  abstract->use();
  EXPECT_FALSE(b);

  Arc<ConcreteForArc> concrete = abstract.downcast<ConcreteForArc>();
  EXPECT_TRUE(abstract == nullptr);
  EXPECT_TRUE(concrete.get() == ref2.get());

  concrete = nullptr;
  EXPECT_FALSE(b);

  ref2 = nullptr;
  EXPECT_TRUE(b);
}

// =======================================================================================
// UniqueArc

struct IncompleteDeclaredForUniqueArc;
static_assert(sizeof(UniqueArc<IncompleteDeclaredForUniqueArc>) == 2 * sizeof(void*));

struct IncompleteDeclaredForUniqueArc: public AtomicRefcounted {
  IncompleteDeclaredForUniqueArc(bool* ptr): ptr(ptr) {}
  ~IncompleteDeclaredForUniqueArc() { *ptr = true; }
  bool* ptr;
};

struct MutableAtomicGadget: public AtomicRefcounted {
  // An atomically refcounted object with state that is only meaningful to mutate before sharing.
  MutableAtomicGadget(bool* destroyed): destroyed(destroyed) {}
  ~MutableAtomicGadget() { *destroyed = true; }

  void setName(kj::StringPtr newName) { name = kj::str(newName); }
  kj::StringPtr getName() const { return name; }

  kj::Arc<MutableAtomicGadget> newRef() const { return addRefToThis(); }

  kj::String name;
  int value = 0;
  bool* destroyed;
};

KJ_TEST("UniqueArc incomplete declared types") {
  bool b = false;
  UniqueArc<IncompleteDeclaredForUniqueArc> ref = kj::uniqueArc<IncompleteDeclaredForUniqueArc>(&b);
  KJ_EXPECT(!b);
  ref = nullptr;
  KJ_EXPECT(b);
}

KJ_TEST("UniqueArc basic lifecycle and mutation") {
  bool destroyed = false;
  {
    UniqueArc<MutableAtomicGadget> unique = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
    KJ_EXPECT(unique != nullptr);
    KJ_EXPECT(!(unique == nullptr));
    KJ_EXPECT(!unique->isShared());

    // Mutable access through all accessors.
    unique->setName("gadget");
    (*unique).value = 42;
    unique.get()->value += 1;
    KJ_EXPECT(unique->getName() == "gadget");
    KJ_EXPECT(unique->value == 43);

    // Const access is also available.
    const auto& cunique = unique;
    KJ_EXPECT(cunique->getName() == "gadget");
    KJ_EXPECT(&*cunique == cunique.get());
    KJ_EXPECT(&*unique == unique.get());

    KJ_EXPECT(!destroyed);
  }
  KJ_EXPECT(destroyed);
}

KJ_TEST("UniqueArc toArc") {
  bool destroyed = false;

  UniqueArc<MutableAtomicGadget> unique = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
  unique->value = 7;
  const MutableAtomicGadget* ptr = unique.get();

  Arc<MutableAtomicGadget> shared = kj::mv(unique).toArc();
  KJ_EXPECT(unique == nullptr);
  KJ_EXPECT(shared != nullptr);
  KJ_EXPECT(shared.get() == ptr);
  KJ_EXPECT(shared->value == 7);
  KJ_EXPECT(!shared->isShared());

  // Behaves like any other Arc from here on.
  Arc<MutableAtomicGadget> shared2 = shared.addRef();
  KJ_EXPECT(shared->isShared());
  Arc<MutableAtomicGadget> shared3 = shared->newRef();

  shared = nullptr;
  KJ_EXPECT(!destroyed);
  shared2 = nullptr;
  KJ_EXPECT(!destroyed);
  shared3 = nullptr;
  KJ_EXPECT(destroyed);
}

#if defined(KJ_ENABLE_IREQUIRE) && KJ_ENABLE_IREQUIRE
KJ_TEST("UniqueArc toArc on null") {
  UniqueArc<MutableAtomicGadget> unique;
  KJ_EXPECT_THROW_MESSAGE("null UniqueArc<> conversion to Arc<>", kj::mv(unique).toArc());
}
#endif

static kj::Arc<MutableAtomicGadget> buildGadget(bool* destroyed, kj::StringPtr name) {
  auto gadget = kj::uniqueArc<MutableAtomicGadget>(destroyed);
  gadget->setName(name);
  return kj::mv(gadget);
}

KJ_TEST("UniqueArc implicit conversion to Arc") {
  bool destroyed = false;
  {
    Arc<MutableAtomicGadget> shared = buildGadget(&destroyed, "built");
    KJ_EXPECT(shared->getName() == "built");
    KJ_EXPECT(!shared->isShared());

    // Also via direct initialization from an rvalue.
    auto unique = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
    Arc<MutableAtomicGadget> shared2 = kj::mv(unique);
    KJ_EXPECT(unique == nullptr);
    KJ_EXPECT(shared2 != nullptr);

    // Conversion to Arc<const T> works too.
    auto unique3 = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
    Arc<const MutableAtomicGadget> shared3 = kj::mv(unique3);
    KJ_EXPECT(unique3 == nullptr);
    KJ_EXPECT(shared3 != nullptr);
  }
  KJ_EXPECT(destroyed);
}

KJ_TEST("UniqueArc move semantics") {
  bool destroyed1 = false;
  bool destroyed2 = false;

  UniqueArc<MutableAtomicGadget> a = kj::uniqueArc<MutableAtomicGadget>(&destroyed1);
  const MutableAtomicGadget* ptr = a.get();

  UniqueArc<MutableAtomicGadget> b = kj::mv(a);
  KJ_EXPECT(a == nullptr);
  KJ_EXPECT(b.get() == ptr);

  // Move-assignment disposes of the previous referent.
  UniqueArc<MutableAtomicGadget> c = kj::uniqueArc<MutableAtomicGadget>(&destroyed2);
  c = kj::mv(b);
  KJ_EXPECT(destroyed2);
  KJ_EXPECT(!destroyed1);
  KJ_EXPECT(b == nullptr);
  KJ_EXPECT(c.get() == ptr);

  // Self-move-assignment is a no-op.
  auto& cref = c;
  c = kj::mv(cref);
  KJ_EXPECT(c.get() == ptr);
  KJ_EXPECT(!destroyed1);

  c = nullptr;
  KJ_EXPECT(c == nullptr);
  KJ_EXPECT(destroyed1);
}

KJ_TEST("UniqueArc inheritance") {
  bool b = false;

  UniqueArc<AtomicChild> child = kj::uniqueArc<AtomicChild>(&b);
  const AtomicChild* ptr = child.get();

  // Up-casting works automatically.
  UniqueArc<AtomicSetTrueInDestructor> parent = kj::mv(child);
  KJ_EXPECT(child == nullptr);
  KJ_EXPECT(parent.get() == ptr);

  // Down-casting is explicit and consumes the source.
  UniqueArc<AtomicChild> down = parent.downcast<AtomicChild>();
  KJ_EXPECT(parent == nullptr);
  KJ_EXPECT(down.get() == ptr);

  // downcast() of a null UniqueArc yields null.
  UniqueArc<AtomicChild> nullDown = parent.downcast<AtomicChild>();
  KJ_EXPECT(nullDown == nullptr);

  // Converting to a base Arc works as well.
  Arc<AtomicSetTrueInDestructor> shared = kj::mv(down);
  KJ_EXPECT(down == nullptr);
  KJ_EXPECT(shared.get() == ptr);

  KJ_EXPECT(!b);
  shared = nullptr;
  KJ_EXPECT(b);
}

KJ_TEST("UniqueArc wraps non-atomic-refcounted types") {
  bool b = false;

  UniqueArc<SetTrueInDestructor2> unique = kj::uniqueArc<SetTrueInDestructor2>(&b);
  KJ_EXPECT(unique != nullptr);
  KJ_EXPECT(&*unique == unique.get());

  // Mutation through the wrapper.
  bool other = false;
  unique->ptr = &other;

  Arc<SetTrueInDestructor2> shared = kj::mv(unique).toArc();
  KJ_EXPECT(unique == nullptr);
  KJ_EXPECT(shared->ptr == &other);

  Arc<SetTrueInDestructor2> shared2 = shared.addRef();
  shared = nullptr;
  KJ_EXPECT(!other);
  shared2 = nullptr;
  KJ_EXPECT(other);
  KJ_EXPECT(!b);
}

KJ_TEST("UniqueArc wraps a value of a non-atomic-refcounted type") {
  bool b = false;

  UniqueArc<SetTrueInDestructor2> unique = SetTrueInDestructor2(&b);
  // The temporary has been moved into the wrapper, so its destructor already fired once. Reset so
  // that we observe the wrapper's destruction below.
  b = false;
  KJ_EXPECT(unique != nullptr);

  Arc<SetTrueInDestructor2> shared = kj::mv(unique);
  KJ_EXPECT(!b);
  shared = nullptr;
  KJ_EXPECT(b);
}

KJ_TEST("UniqueArc<String>") {
  UniqueArc<String> unique = kj::uniqueArc<String>(kj::str("hello"));
  KJ_EXPECT(unique->asPtr() == "hello");

  *unique = kj::str("world");
  KJ_EXPECT(unique->asPtr() == "world");

  Arc<String> shared = kj::mv(unique).toArc();
  KJ_EXPECT(unique == nullptr);
  KJ_EXPECT(shared->asPtr() == "world");
}

KJ_TEST("UniqueArc<Abstract>") {
  bool b = false;

  UniqueArc<AbstractForArc> unique = kj::uniqueArc<ConcreteForArc>(&b);
  KJ_EXPECT(unique != nullptr);
  unique->use();

  Arc<AbstractForArc> shared = kj::mv(unique);
  KJ_EXPECT(unique == nullptr);
  shared->use();

  KJ_EXPECT(!b);
  shared = nullptr;
  KJ_EXPECT(b);
}

KJ_TEST("UniqueArc handed off to another thread") {
  // A UniqueArc may be moved to another thread (like Own<T>), which may then mutate the object and
  // share it.
  bool destroyed = false;
  {
    auto unique = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
    unique->value = 1;

    kj::MutexGuarded<kj::Maybe<kj::Arc<MutableAtomicGadget>>> result;
    {
      kj::Thread thread([unique = kj::mv(unique), &result]() mutable noexcept {
        unique->value += 1;
        unique->setName("from thread");
        *result.lockExclusive() = kj::mv(unique).toArc();
      });
    }

    auto shared = KJ_ASSERT_NONNULL(kj::mv(*result.lockExclusive()));
    KJ_EXPECT(shared->value == 2);
    KJ_EXPECT(shared->getName() == "from thread");
    KJ_EXPECT(!destroyed);
  }
  KJ_EXPECT(destroyed);
}

#if defined(KJ_ENABLE_IREQUIRE) && KJ_ENABLE_IREQUIRE
KJ_TEST("UniqueArc detects broken uniqueness") {
  bool destroyed = false;
  {
    auto unique = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
    const auto& cunique = unique;
    // Raw pointer so that we can observe the object without going through the checking accessors.
    const MutableAtomicGadget* raw = cunique.get();

    // The object illegally hands out a reference to itself while uniquely owned.
    Arc<MutableAtomicGadget> leaked = unique->newRef();
    KJ_EXPECT(raw->isShared());

    // Every accessor, mutable or const, asserts uniqueness.
    KJ_EXPECT_THROW_MESSAGE("UniqueArc<> is no longer unique", unique->value = 1);
    KJ_EXPECT_THROW_MESSAGE("UniqueArc<> is no longer unique", (*unique).value = 1);
    KJ_EXPECT_THROW_MESSAGE("UniqueArc<> is no longer unique", unique.get());
    KJ_EXPECT_THROW_MESSAGE("UniqueArc<> is no longer unique", cunique->value);
    KJ_EXPECT_THROW_MESSAGE("UniqueArc<> is no longer unique", (*cunique).value);
    KJ_EXPECT_THROW_MESSAGE("UniqueArc<> is no longer unique", cunique.get());
    KJ_EXPECT_THROW_MESSAGE("UniqueArc<> is no longer unique", kj::mv(unique).toArc());
    // The failed conversion did not consume the UniqueArc.
    KJ_EXPECT(unique != nullptr);

    // Once the extra reference is gone, uniqueness is restored.
    leaked = nullptr;
    KJ_EXPECT(!raw->isShared());
    KJ_EXPECT(cunique->value == 0);
    KJ_EXPECT(cunique.get() == raw);
    unique->value = 2;
    Arc<MutableAtomicGadget> shared = kj::mv(unique).toArc();
    KJ_EXPECT(shared->value == 2);
  }
  KJ_EXPECT(destroyed);

  UniqueArc<MutableAtomicGadget> nullUnique;
  KJ_EXPECT_THROW_MESSAGE("null UniqueArc<> dereference", nullUnique->value = 1);
}
#endif

// Maybe<UniqueArc<T>> is niche-optimized: a null UniqueArc is the "none" state, so no extra flag is
// stored.
static_assert(NicheOptimizable<UniqueArc<MutableAtomicGadget>>);
static_assert(sizeof(Maybe<UniqueArc<MutableAtomicGadget>>) == sizeof(UniqueArc<MutableAtomicGadget>));
static_assert(sizeof(Maybe<UniqueArc<IncompleteDeclaredAtomicRefcounted>>) == 2 * sizeof(void*));
static_assert(sizeof(Maybe<UniqueArc<IncompleteDeclaredNotAtomicRefcounted>>) == 2 * sizeof(void*));

// UniqueArc is move-only, so Maybe<UniqueArc<T>> is not cloneable.
static_assert(!Cloneable<Maybe<UniqueArc<MutableAtomicGadget>>>);

// Maybe<UniqueArc<T>> does not implicitly convert to a reference to the referent.
static_assert(!canConvert<Maybe<UniqueArc<MutableAtomicGadget>>&, Maybe<MutableAtomicGadget&>>());
static_assert(!canConvert<Maybe<UniqueArc<MutableAtomicGadget>>&,
                          Maybe<const MutableAtomicGadget&>>());
static_assert(!canConvert<const Maybe<UniqueArc<MutableAtomicGadget>>&,
                          Maybe<const MutableAtomicGadget&>>());
static_assert(!canConvert<Maybe<UniqueArc<AtomicChild>>&, Maybe<AtomicSetTrueInDestructor&>>());

KJ_TEST("Maybe<UniqueArc<T>> niche optimization") {
  bool destroyed = false;

  {
    Maybe<UniqueArc<MutableAtomicGadget>> maybe;
    KJ_EXPECT(maybe == kj::none);

    maybe = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
    KJ_EXPECT(maybe != kj::none);
    KJ_IF_SOME(ref, maybe) {
      KJ_EXPECT(ref.get() != nullptr);
      // Mutable access through the Maybe.
      ref->value = 5;
      ref->setName("in maybe");
      KJ_EXPECT(ref->value == 5);
      KJ_EXPECT(ref->getName() == "in maybe");
    } else {
      KJ_FAIL_EXPECT("expected value");
    }

    // Moving out leaves the source in the none state.
    Maybe<UniqueArc<MutableAtomicGadget>> moved = kj::mv(maybe);
    KJ_EXPECT(maybe == kj::none);
    KJ_EXPECT(moved != kj::none);
    KJ_EXPECT(!destroyed);

    // Move-assignment.
    maybe = kj::mv(moved);
    KJ_EXPECT(moved == kj::none);
    KJ_EXPECT(maybe != kj::none);
    KJ_EXPECT(!destroyed);

    // Setting to none releases the object.
    maybe = kj::none;
    KJ_EXPECT(maybe == kj::none);
    KJ_EXPECT(destroyed);
  }

  {
    // Storing a null UniqueArc yields none, consistent with the niche representation.
    Maybe<UniqueArc<MutableAtomicGadget>> maybe = UniqueArc<MutableAtomicGadget>();
    KJ_EXPECT(maybe == kj::none);
    maybe = UniqueArc<MutableAtomicGadget>(nullptr);
    KJ_EXPECT(maybe == kj::none);
  }

  {
    // emplace()
    destroyed = false;
    Maybe<UniqueArc<MutableAtomicGadget>> maybe;
    auto& ref = maybe.emplace(kj::uniqueArc<MutableAtomicGadget>(&destroyed));
    KJ_EXPECT(ref->destroyed == &destroyed);
    KJ_EXPECT(maybe != kj::none);
    KJ_EXPECT(!destroyed);

    // Emplacing over an existing value releases the old one.
    bool destroyed2 = false;
    maybe.emplace(kj::uniqueArc<MutableAtomicGadget>(&destroyed2));
    KJ_EXPECT(destroyed);
    KJ_EXPECT(!destroyed2);
    maybe = kj::none;
    KJ_EXPECT(destroyed2);
  }

  {
    // Destructor releases the object.
    destroyed = false;
    {
      Maybe<UniqueArc<MutableAtomicGadget>> maybe = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
      KJ_EXPECT(!destroyed);
    }
    KJ_EXPECT(destroyed);
  }

  {
    // Moving the UniqueArc out of the Maybe and sharing it.
    destroyed = false;
    Maybe<UniqueArc<MutableAtomicGadget>> maybe = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
    Arc<MutableAtomicGadget> shared = KJ_ASSERT_NONNULL(kj::mv(maybe)).toArc();
    KJ_EXPECT(maybe == kj::none);
    KJ_EXPECT(shared != nullptr);
    KJ_EXPECT(!destroyed);
    shared = nullptr;
    KJ_EXPECT(destroyed);
  }
}

KJ_TEST("Maybe<UniqueArc<T>> converting constructor from UniqueArc<Derived>") {
  bool b = false;

  // Implicit conversion UniqueArc<AtomicChild> -> Maybe<UniqueArc<AtomicSetTrueInDestructor>> via
  // copy-initialization.
  auto child = kj::uniqueArc<AtomicChild>(&b);
  const AtomicChild* ptr = child.get();
  Maybe<UniqueArc<AtomicSetTrueInDestructor>> maybe = kj::mv(child);
  KJ_EXPECT(child == nullptr);
  KJ_EXPECT(maybe != kj::none);
  KJ_IF_SOME(ref, maybe) {
    KJ_EXPECT(ref.get() == ptr);
  }

  // Converting assignment.
  bool b2 = false;
  Maybe<UniqueArc<AtomicSetTrueInDestructor>> maybe2;
  maybe2 = kj::uniqueArc<AtomicChild>(&b2);
  KJ_EXPECT(maybe2 != kj::none);

  // Maybe<UniqueArc<AtomicChild>> -> Maybe<UniqueArc<AtomicSetTrueInDestructor>>.
  bool b3 = false;
  Maybe<UniqueArc<AtomicChild>> maybeChild = kj::uniqueArc<AtomicChild>(&b3);
  Maybe<UniqueArc<AtomicSetTrueInDestructor>> maybe3 = kj::mv(maybeChild);
  KJ_EXPECT(maybeChild == kj::none);
  KJ_EXPECT(maybe3 != kj::none);

  KJ_EXPECT(!b);
  maybe = kj::none;
  KJ_EXPECT(b);
  KJ_EXPECT(!b2);
  maybe2 = kj::none;
  KJ_EXPECT(b2);
  KJ_EXPECT(!b3);
  maybe3 = kj::none;
  KJ_EXPECT(b3);
}

KJ_TEST("Maybe<Arc<T>> converting constructor from UniqueArc") {
  // Since UniqueArc<U> implicitly converts to Arc<T>, it also implicitly converts to Maybe<Arc<T>>.
  bool destroyed = false;

  auto unique = kj::uniqueArc<MutableAtomicGadget>(&destroyed);
  unique->value = 9;
  const MutableAtomicGadget* ptr = unique.get();

  Maybe<Arc<MutableAtomicGadget>> maybe = kj::mv(unique);
  KJ_EXPECT(unique == nullptr);
  KJ_IF_SOME(ref, maybe) {
    KJ_EXPECT(ref.get() == ptr);
    KJ_EXPECT(ref->value == 9);
  } else {
    KJ_FAIL_EXPECT("expected value");
  }

  // Converting assignment, including up-casting.
  bool b = false;
  Maybe<Arc<AtomicSetTrueInDestructor>> maybe2;
  maybe2 = kj::uniqueArc<AtomicChild>(&b);
  KJ_EXPECT(maybe2 != kj::none);

  KJ_EXPECT(!destroyed);
  maybe = kj::none;
  KJ_EXPECT(destroyed);
  KJ_EXPECT(!b);
  maybe2 = kj::none;
  KJ_EXPECT(b);
}

// A refcounted object that holds a self weak-reference and touches it from its destructor. This
// exercises the requirement that weak references remain valid while the destructor runs, even when
// the destructor's own weak reference is the last one keeping the backing cell alive.
struct WeakInDestructor: public Refcounted {
  WeakInDestructor(bool* ptr): ptr(ptr) {}
  ~WeakInDestructor() {
    // At this point the strong refcount has already reached zero. Cloning the weak reference (and
    // observing that it has expired) must not touch freed memory.
    auto clone = self.clone();
    KJ_EXPECT(clone == nullptr);
    KJ_IF_SOME(obj, clone.tryGet()) {
      KJ_FAIL_EXPECT("weak ref should have expired during destruction", &obj);
    }
    *ptr = true;
  }

  kj::WeakRc<WeakInDestructor> self = nullptr;

  bool* ptr;
};

KJ_TEST("WeakRc manipulated during destructor stays valid") {
  bool destroyed = false;
  {
    Rc<WeakInDestructor> ref = kj::rc<WeakInDestructor>(&destroyed);
    // The object's only weak reference is the one it holds to itself. When the last strong Rc is
    // dropped below, the destructor clones this weak reference; the backing cell must survive until
    // the destructor finishes.
    ref->self = ref.downgrade();
    KJ_EXPECT(!destroyed);
  }
  KJ_EXPECT(destroyed);
}

}  // namespace _
}  // namespace kj
