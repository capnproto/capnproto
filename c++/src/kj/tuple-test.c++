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
#include "memory.h"
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
}

}  // namespace kj
