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

#include "tuple.h"
#include "array.h"
#include "memory.h"
#include "refcount.h"
#include "string.h"
#include <kj/compat/gtest.h>

namespace kj {

struct Foo { uint foo; Foo(uint i): foo(i) {} };
struct Bar { uint bar; Bar(uint i): bar(i) {} };
struct Baz { uint baz; Baz(uint i): baz(i) {} };
struct Qux { uint qux; Qux(uint i): qux(i) {} };
struct Quux { uint quux; Quux(uint i): quux(i) {} };

TEST(Tuple, Tuple) {
  Tuple<Foo, Bar> t = tuple(Foo(123), Bar(456));
  EXPECT_EQ(123u, get<0>(t).foo);
  EXPECT_EQ(456u, get<1>(t).bar);

  Tuple<Foo, Bar, Baz, Qux, Quux> t2 = tuple(t, Baz(789), tuple(Qux(321), Quux(654)));
  EXPECT_EQ(123u, get<0>(t2).foo);
  EXPECT_EQ(456u, get<1>(t2).bar);
  EXPECT_EQ(789u, get<2>(t2).baz);
  EXPECT_EQ(321u, get<3>(t2).qux);
  EXPECT_EQ(654u, get<4>(t2).quux);

  Tuple<Own<Foo>, Own<Bar>> t3 = tuple(heap<Foo>(123), heap<Bar>(456));
  EXPECT_EQ(123u, get<0>(t3)->foo);
  EXPECT_EQ(456u, get<1>(t3)->bar);

  Tuple<Own<Foo>, Own<Bar>, Own<Baz>, Own<Qux>, Own<Quux>> t4 =
      tuple(mv(t3), heap<Baz>(789), tuple(heap<Qux>(321), heap<Quux>(654)));
  EXPECT_EQ(123u, get<0>(t4)->foo);
  EXPECT_EQ(456u, get<1>(t4)->bar);
  EXPECT_EQ(789u, get<2>(t4)->baz);
  EXPECT_EQ(321u, get<3>(t4)->qux);
  EXPECT_EQ(654u, get<4>(t4)->quux);

  Tuple<String, StringPtr> t5 = tuple(heapString("foo"), "bar");
  EXPECT_EQ("foo", get<0>(t5));
  EXPECT_EQ("bar", get<1>(t5));

  Tuple<StringPtr, StringPtr, StringPtr, StringPtr, String> t6 =
      tuple(Tuple<StringPtr, StringPtr>(t5), "baz", tuple("qux", heapString("quux")));
  EXPECT_EQ("foo", get<0>(t6));
  EXPECT_EQ("bar", get<1>(t6));
  EXPECT_EQ("baz", get<2>(t6));
  EXPECT_EQ("qux", get<3>(t6));
  EXPECT_EQ("quux", get<4>(t6));

  kj::apply([](Foo a, Bar b, Own<Foo> c, Own<Bar> d, uint e, StringPtr f, StringPtr g) {
    EXPECT_EQ(123u, a.foo);
    EXPECT_EQ(456u, b.bar);
    EXPECT_EQ(123u, c->foo);
    EXPECT_EQ(456u, d->bar);
    EXPECT_EQ(789u, e);
    EXPECT_EQ("foo", f);
    EXPECT_EQ("bar", g);
  }, t, tuple(heap<Foo>(123), heap<Bar>(456)), 789, mv(t5));

  uint i = tuple(123);
  EXPECT_EQ(123u, i);

  i = tuple(tuple(), 456, tuple(tuple(), tuple()));
  EXPECT_EQ(456u, i);

  EXPECT_EQ(0, (indexOfType<int, Tuple<int, char, bool>>()));
  EXPECT_EQ(1, (indexOfType<char, Tuple<int, char, bool>>()));
  EXPECT_EQ(2, (indexOfType<bool, Tuple<int, char, bool>>()));
  EXPECT_EQ(0, (indexOfType<int, int>()));
}

struct TupleQualifiedConversion {
  int value = 123;
  operator int() & { return value + 1; }
  operator int() const & { return value + 2; }
  operator int() && { int result = value + 3; value = 0; return result; }
};

struct TupleExplicitConversion {
  explicit operator int() const;
};

struct TupleExplicitConstructor {
  explicit TupleExplicitConstructor(int);
};

struct TupleMutableCopy {
  explicit TupleMutableCopy(int value): value(value) {}
  TupleMutableCopy(TupleMutableCopy&) = default;
  TupleMutableCopy(TupleMutableCopy&&) = default;
  int value;
};

struct TupleImmovableConversion {
  TupleImmovableConversion() = default;
  KJ_DISALLOW_COPY_AND_MOVE(TupleImmovableConversion);
  operator int() && { return value; }
  int value = 123;
};

struct TupleImmovableConstructor {
  TupleImmovableConstructor(int value): value(value) {}
  KJ_DISALLOW_COPY_AND_MOVE(TupleImmovableConstructor);
  int value;
};

static_assert(canConvert<Tuple<String, Array<int>>&, Tuple<StringPtr, ArrayPtr<int>>>());
static_assert(canConvert<const Tuple<String, Array<int>>&,
                        Tuple<StringPtr, ArrayPtr<const int>>>());
static_assert(!canConvert<const Tuple<String, Array<int>>&, Tuple<StringPtr, ArrayPtr<int>>>());
static_assert(!canConvert<Tuple<String, int>&, Tuple<int, long>>());
static_assert(!canConvert<Tuple<int, int, int>, Tuple<long, long>>());
static_assert(!canConvert<Tuple<int, int>, Tuple<long, long, long>>());
static_assert(!canConvert<Tuple<TupleExplicitConversion, int>, Tuple<int, long>>());
static_assert(!canConvert<Tuple<int, int>, Tuple<TupleExplicitConstructor, long>>());
static_assert(canConvert<Tuple<TupleMutableCopy, int>&, Tuple<TupleMutableCopy, long>>());
static_assert(!canConvert<const Tuple<TupleMutableCopy, int>&, Tuple<TupleMutableCopy, long>>());
static_assert(canConvert<Tuple<TupleImmovableConversion, int>, Tuple<int, long>>());
static_assert(canConvert<Tuple<int, int>, Tuple<TupleImmovableConstructor, long>>());
static_assert(!canConvert<Tuple<String&, String&>, Tuple<String, String>>());

KJ_TEST("Tuple converts owning elements directly to borrowed pointer elements") {
  auto owner = kj::tuple(kj::str("hello"), kj::heapArray<int>({12, 34}));
  Tuple<StringPtr, ArrayPtr<int>> view = owner;
  KJ_EXPECT(get<0>(view).cStr() == get<0>(owner).cStr());
  KJ_EXPECT(get<1>(view).begin() == get<1>(owner).begin());
  get<1>(view)[0] = 56;
  KJ_EXPECT(get<1>(owner)[0] == 56);

  const auto& constOwner = owner;
  Tuple<StringPtr, ArrayPtr<const int>> readonly = constOwner;
  KJ_EXPECT(get<0>(readonly) == "hello");
  KJ_EXPECT(get<1>(readonly)[1] == 34);
}

KJ_TEST("Tuple conversions respect source reference qualification") {
  auto source = kj::tuple(TupleQualifiedConversion{}, short(456));
  Tuple<int, long> mutableValue = source;
  KJ_EXPECT(get<0>(mutableValue) == 124);
  KJ_EXPECT(get<1>(mutableValue) == 456);
  const auto& constSource = source;
  Tuple<int, long> constValue = constSource;
  KJ_EXPECT(get<0>(constValue) == 125);
  Tuple<int, long> movedValue = kj::mv(source);
  KJ_EXPECT(get<0>(movedValue) == 126);
  KJ_EXPECT(get<0>(source).value == 0);

  auto references = kj::refTuple(get<0>(source), get<1>(source));
  Tuple<int, long> copiedReferents = kj::mv(references);
  KJ_EXPECT(get<0>(copiedReferents) == 1);
  KJ_EXPECT(get<0>(source).value == 0);
}

KJ_TEST("Tuple element construction supports mutable copying and immovable values") {
  auto source = kj::tuple(TupleMutableCopy(123), 456);
  Tuple<TupleMutableCopy, long> copy = source;
  KJ_EXPECT(get<0>(copy).value == 123);
  KJ_EXPECT(get<1>(copy) == 456);
  auto sameTypeCopy = source;
  KJ_EXPECT(get<0>(sameTypeCopy).value == 123);

  // Conversion does not move the source into an intermediate value.
  Tuple<TupleImmovableConversion, int> immovableSource;
  get<1>(immovableSource) = 456;
  Tuple<int, long> converted = kj::mv(immovableSource);
  KJ_EXPECT(get<0>(converted) == 123);
  KJ_EXPECT(get<1>(converted) == 456);

  // Nor does it create a temporary destination element that would need to be moved.
  Tuple<TupleImmovableConstructor, long> immovableDestination = kj::tuple(123, 456);
  KJ_EXPECT(get<0>(immovableDestination).value == 123);
  KJ_EXPECT(get<1>(immovableDestination) == 456);
}

KJ_TEST("Tuple propagates owning-to-pointer Rc and Arc conversions") {
  auto source = kj::tuple(kj::rc<String>(kj::str("hello")),
      kj::arc<Array<int>>(kj::heapArray<int>({12, 34})));
  Tuple<Rc<StringPtr>, Arc<ArrayPtr<const int>>> converted = kj::mv(source);
  KJ_EXPECT(get<0>(source) == nullptr);
  KJ_EXPECT(get<1>(source) == nullptr);
  KJ_EXPECT(*get<0>(converted) == "hello");
  KJ_EXPECT((*get<1>(converted))[1] == 34);
}

KJ_TEST("Tuple conversion destroys completed elements if a later conversion throws") {
  struct Owner {
    Owner(bool& destroyed): destroyed(destroyed) {}
    ~Owner() noexcept(false) { destroyed = true; }
    bool& destroyed;
  };
  struct ThrowingConversion {
    operator int() const { KJ_FAIL_REQUIRE("tuple conversion failed"); }
  };

  bool destroyed = false;
  auto source = kj::tuple(kj::heap<Owner>(destroyed), ThrowingConversion{});
  using Converted = Tuple<Own<Owner>, int>;
  KJ_EXPECT_THROW_MESSAGE("tuple conversion failed", {
    Converted converted = kj::mv(source);
  });
  KJ_EXPECT(get<0>(source).get() == nullptr);
  KJ_EXPECT(destroyed);
}

TEST(Tuple, RefTuple) {
  uint i = 123;
  StringPtr s = "foo";

  Tuple<uint&, StringPtr&, uint, StringPtr> t = refTuple(i, s, 321, "bar");
  EXPECT_EQ(get<0>(t), 123);
  EXPECT_EQ(get<1>(t), "foo");
  EXPECT_EQ(get<2>(t), 321);
  EXPECT_EQ(get<3>(t), "bar");

  i = 456;
  s = "baz";

  EXPECT_EQ(get<0>(t), 456);
  EXPECT_EQ(get<1>(t), "baz");
  EXPECT_EQ(get<2>(t), 321);
  EXPECT_EQ(get<3>(t), "bar");
}

}  // namespace kj
