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

}  // namespace
}  // namespace kj
