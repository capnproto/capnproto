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

#include "tuple.h"
#include "memory.h"
#include "string.h"
#include <gtest/gtest.h>

namespace kj {

struct Foo { uint foo; Foo(uint i): foo(i) {} };
struct Bar { uint bar; Bar(uint i): bar(i) {} };
struct Baz { uint baz; Baz(uint i): baz(i) {} };
struct Qux { uint qux; Qux(uint i): qux(i) {} };
struct Quux { uint quux; Quux(uint i): quux(i) {} };

TEST(Tuple, Tuple) {
  Tuple<Foo, Bar> t = tuple(Foo(123), Bar(456));
  EXPECT_EQ(123, get<0>(t).foo);
  EXPECT_EQ(456, get<1>(t).bar);

  Tuple<Foo, Bar, Baz, Qux, Quux> t2 = tuple(t, Baz(789), tuple(Qux(321), Quux(654)));
  EXPECT_EQ(123, get<0>(t2).foo);
  EXPECT_EQ(456, get<1>(t2).bar);
  EXPECT_EQ(789, get<2>(t2).baz);
  EXPECT_EQ(321, get<3>(t2).qux);
  EXPECT_EQ(654, get<4>(t2).quux);

  Tuple<Own<Foo>, Own<Bar>> t3 = tuple(heap<Foo>(123), heap<Bar>(456));
  EXPECT_EQ(123, get<0>(t3)->foo);
  EXPECT_EQ(456, get<1>(t3)->bar);

  Tuple<Own<Foo>, Own<Bar>, Own<Baz>, Own<Qux>, Own<Quux>> t4 =
      tuple(mv(t3), heap<Baz>(789), tuple(heap<Qux>(321), heap<Quux>(654)));
  EXPECT_EQ(123, get<0>(t4)->foo);
  EXPECT_EQ(456, get<1>(t4)->bar);
  EXPECT_EQ(789, get<2>(t4)->baz);
  EXPECT_EQ(321, get<3>(t4)->qux);
  EXPECT_EQ(654, get<4>(t4)->quux);

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
    EXPECT_EQ(123, a.foo);
    EXPECT_EQ(456, b.bar);
    EXPECT_EQ(123, c->foo);
    EXPECT_EQ(456, d->bar);
    EXPECT_EQ(789, e);
    EXPECT_EQ("foo", f);
    EXPECT_EQ("bar", g);
  }, t, tuple(heap<Foo>(123), heap<Bar>(456)), 789, mv(t5));

  uint i = tuple(123);
  EXPECT_EQ(123, i);

  i = tuple(tuple(), 456, tuple(tuple(), tuple()));
  EXPECT_EQ(456, i);
}

}  // namespace kj
