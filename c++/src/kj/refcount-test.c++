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
#include "array.h"
#include "string.h"
#include <kj/compat/gtest.h>

namespace kj {

namespace _ {
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
