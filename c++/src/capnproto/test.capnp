# Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

using Cxx = import "c++.capnp";

$Cxx.namespace("capnproto::test");

enum TestEnum {
  foo @0;
  bar @1;
  baz @2;
  qux @3;
  quux @4;
  corge @5;
  grault @6;
  garply @7;
}

struct TestAllTypes {
  voidField      @0  : Void;
  boolField      @1  : Bool;
  int8Field      @2  : Int8;
  int16Field     @3  : Int16;
  int32Field     @4  : Int32;
  int64Field     @5  : Int64;
  uInt8Field     @6  : UInt8;
  uInt16Field    @7  : UInt16;
  uInt32Field    @8  : UInt32;
  uInt64Field    @9  : UInt64;
  float32Field   @10 : Float32;
  float64Field   @11 : Float64;
  textField      @12 : Text;
  dataField      @13 : Data;
  structField    @14 : TestAllTypes;
  enumField      @15 : TestEnum;
  interfaceField @16 : Void;  # TODO

  voidList      @17 : List(Void);
  boolList      @18 : List(Bool);
  int8List      @19 : List(Int8);
  int16List     @20 : List(Int16);
  int32List     @21 : List(Int32);
  int64List     @22 : List(Int64);
  uInt8List     @23 : List(UInt8);
  uInt16List    @24 : List(UInt16);
  uInt32List    @25 : List(UInt32);
  uInt64List    @26 : List(UInt64);
  float32List   @27 : List(Float32);
  float64List   @28 : List(Float64);
  textList      @29 : List(Text);
  dataList      @30 : List(Data);
  structList    @31 : List(TestAllTypes);
  enumList      @32 : List(TestEnum);
  interfaceList @33 : List(Void);  # TODO
}

struct TestDefaults {
  voidField      @0  : Void    = void;
  boolField      @1  : Bool    = true;
  int8Field      @2  : Int8    = -123;
  int16Field     @3  : Int16   = -12345;
  int32Field     @4  : Int32   = -12345678;
  int64Field     @5  : Int64   = -123456789012345;
  uInt8Field     @6  : UInt8   = 234;
  uInt16Field    @7  : UInt16  = 45678;
  uInt32Field    @8  : UInt32  = 3456789012;
  uInt64Field    @9  : UInt64  = 12345678901234567890;
  float32Field   @10 : Float32 = 1234.5;
  float64Field   @11 : Float64 = -123e45;
  textField      @12 : Text    = "foo";
  dataField      @13 : Data    = "bar";
  structField    @14 : TestAllTypes = (
      voidField      = void,
      boolField      = true,
      int8Field      = -12,
      int16Field     = 3456,
      int32Field     = -78901234,
      int64Field     = 56789012345678,
      uInt8Field     = 90,
      uInt16Field    = 1234,
      uInt32Field    = 56789012,
      uInt64Field    = 345678901234567890,
      float32Field   = -1.25e-10,
      float64Field   = 345,
      textField      = "baz",
      dataField      = "qux",
      structField    = (
          textField = "nested",
          structField = (textField = "really nested")),
      enumField      = baz,
      # interfaceField can't have a default

      voidList      = [void, void, void],
      boolList      = [false, true, false, true, true],
      int8List      = [12, -34, -0x80, 0x7f],
      int16List     = [1234, -5678, -0x8000, 0x7fff],
      int32List     = [12345678, -90123456, -0x8000000, 0x7ffffff],
      int64List     = [123456789012345, -678901234567890, -0x8000000000000000, 0x7fffffffffffffff],
      uInt8List     = [12, 34, 0, 0xff],
      uInt16List    = [1234, 5678, 0, 0xffff],
      uInt32List    = [12345678, 90123456, 0, 0xffffffff],
      uInt64List    = [123456789012345, 678901234567890, 0, 0xffffffffffffffff],
      float32List   = [0, 1234567, 1e37, -1e37, 1e-37, -1e-37],
      float64List   = [0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306],
      textList      = ["quux", "corge", "grault"],
      dataList      = ["garply", "waldo", "fred"],
      structList    = [
          (textField = "x structlist 1"),
          (textField = "x structlist 2"),
          (textField = "x structlist 3")],
      enumList      = [qux, bar, grault]
      # interfaceList can't have a default
      );
  enumField      @15 : TestEnum = corge;
  interfaceField @16 : Void;  # TODO

  voidList      @17 : List(Void)    = [void, void, void, void, void, void];
  boolList      @18 : List(Bool)    = [true, false, false, true];
  int8List      @19 : List(Int8)    = [111, -111];
  int16List     @20 : List(Int16)   = [11111, -11111];
  int32List     @21 : List(Int32)   = [111111111, -111111111];
  int64List     @22 : List(Int64)   = [1111111111111111111, -1111111111111111111];
  uInt8List     @23 : List(UInt8)   = [111, 222] ;
  uInt16List    @24 : List(UInt16)  = [33333, 44444];
  uInt32List    @25 : List(UInt32)  = [3333333333];
  uInt64List    @26 : List(UInt64)  = [11111111111111111111];
  float32List   @27 : List(Float32) = [5555.5, inf, -inf, nan];
  float64List   @28 : List(Float64) = [7777.75, inf, -inf, nan];
  textList      @29 : List(Text)    = ["plugh", "xyzzy", "thud"];
  dataList      @30 : List(Data)    = ["oops", "exhausted", "rfc3092"];
  structList    @31 : List(TestAllTypes) = [
      (textField = "structlist 1"),
      (textField = "structlist 2"),
      (textField = "structlist 3")];
  enumList      @32 : List(TestEnum) = [foo, garply];
  interfaceList @33 : List(Void);  # TODO
}

struct TestUnion {
  union0 @0 union {
    # Pack union 0 under ideal conditions: there is no unused padding space prior to it.
    u0f0s0  @4: Void;
    u0f0s1  @5: Bool;
    u0f0s8  @6: Int8;
    u0f0s16 @7: Int16;
    u0f0s32 @8: Int32;
    u0f0s64 @9: Int64;
    u0f0sp  @10: Text;

    # Pack more stuff into union0 -- should go in same space.
    u0f1s0  @11: Void;
    u0f1s1  @12: Bool;
    u0f1s8  @13: Int8;
    u0f1s16 @14: Int16;
    u0f1s32 @15: Int32;
    u0f1s64 @16: Int64;
    u0f1sp  @17: Text;
  }

  # Pack one bit in order to make pathological situation for union1.
  bit0 @18: Bool;

  union1 @1 union {
    # Pack pathologically bad case.  Each field takes up new space.
    u1f0s0  @19: Void;
    u1f0s1  @20: Bool;
    u1f1s1  @21: Bool;
    u1f0s8  @22: Int8;
    u1f1s8  @23: Int8;
    u1f0s16 @24: Int16;
    u1f1s16 @25: Int16;
    u1f0s32 @26: Int32;
    u1f1s32 @27: Int32;
    u1f0s64 @28: Int64;
    u1f1s64 @29: Int64;
    u1f0sp  @30: Text;
    u1f1sp  @31: Text;

    # Pack more stuff into union1 -- should go into same space as u1f0s64.
    u1f2s0  @32: Void;
    u1f2s1  @33: Bool;
    u1f2s8  @34: Int8;
    u1f2s16 @35: Int16;
    u1f2s32 @36: Int32;
    u1f2s64 @37: Int64;
    u1f2sp  @38: Text;
  }

  # Fill in the rest of that bitfield from earlier.
  bit2 @39: Bool;
  bit3 @40: Bool;
  bit4 @41: Bool;
  bit5 @42: Bool;
  bit6 @43: Bool;
  bit7 @44: Bool;

  # Interleave two unions to be really annoying.
  # Also declare in reverse order to make sure union discriminant values are sorted by field number
  # and not by declaration order.
  union2 @2 union {
    u2f0s64 @54: Int64;
    u2f0s32 @52: Int32;
    u2f0s16 @50: Int16;
    u2f0s8 @47: Int8;
    u2f0s1 @45: Bool;
  }

  union3 @3 union {
    u3f0s64 @55: Int64;
    u3f0s32 @53: Int32;
    u3f0s16 @51: Int16;
    u3f0s8 @48: Int8;
    u3f0s1 @46: Bool;
  }

  byte0 @49: UInt8;
}

struct TestUnionDefaults {
  s16s8s64s8Set @0 :TestUnion =
      (union0 = u0f0s16(321), union1 = u1f0s8(123), union2 = u2f0s64(12345678901234567),
       union3 = u3f0s8(55));
  s0sps1s32Set @1 :TestUnion =
      (union0 = u0f1s0(void), union1 = u1f0sp("foo"), union2 = u2f0s1(true),
       union3 = u3f0s32(12345678));
}

struct TestNestedTypes {
  enum NestedEnum {
    foo @0;
    bar @1;
  }

  struct NestedStruct {
    enum NestedEnum {
      baz @0;
      qux @1;
      quux @2;
    }

    outerNestedEnum @0 :TestNestedTypes.NestedEnum = bar;
    innerNestedEnum @1 :NestedEnum = quux;
  }

  nestedStruct @0 :NestedStruct;

  outerNestedEnum @1 :NestedEnum = bar;
  innerNestedEnum @2 :NestedStruct.NestedEnum = quux;
}

struct TestUsing {
  using OuterNestedEnum = TestNestedTypes.NestedEnum;
  using TestNestedTypes.NestedStruct.NestedEnum;

  outerNestedEnum @1 :OuterNestedEnum = bar;
  innerNestedEnum @0 :NestedEnum = quux;
}

struct TestInline0 fixed(0 bits) { f @0: Void; }
struct TestInline1 fixed(1 bits) { f @0: Bool; }
struct TestInline8 fixed(8 bits) { f0 @0: Bool; f1 @1: Bool; f2 @2: Bool; }
struct TestInline16 fixed(16 bits) { f0 @0: UInt8; f1 @1: UInt8; }
struct TestInline32 fixed(32 bits) { f0 @0: UInt8; f1 @1: UInt16; }
struct TestInline64 fixed(64 bits) { f0 @0: UInt8; f1 @1: UInt32; }
struct TestInline128 fixed(2 words) { f0 @0: UInt64; f1 @1: UInt64; }
struct TestInline192 fixed(3 words) { f0 @0: UInt64; f1 @1: UInt64; f2 @2: UInt64; }

struct TestInline0p fixed(0 bits, 1 pointers) { f @0 :Inline(TestInline0); p0 @1 :Text; }
struct TestInline1p fixed(1 bits, 1 pointers) { f @0 :Inline(TestInline1); p0 @1 :Text; }
struct TestInline8p fixed(8 bits, 1 pointers) { f @0 :Inline(TestInline8); p0 @1 :Text; }
struct TestInline16p fixed(16 bits, 2 pointers) { f @0 :Inline(TestInline16); p0 @1 :Text; p1 @2 :Text; }
struct TestInline32p fixed(32 bits, 2 pointers) { f @0 :Inline(TestInline32); p0 @1 :Text; p1 @2 :Text; }
struct TestInline64p fixed(64 bits, 2 pointers) { f @0 :Inline(TestInline64); p0 @1 :Text; p1 @2 :Text; }
struct TestInline128p fixed(2 words, 3 pointers) { f @0 :Inline(TestInline128); p0 @1 :Text; p1 @2 :Text; p2 @3 :Text; }
struct TestInline192p fixed(3 words, 3 pointers) { f @0 :Inline(TestInline192); p0 @1 :Text; p1 @2 :Text; p2 @3 :Text; }

struct TestInlineLayout {
  f0 @0 :Inline(TestInline0);
  f1 @1 :Inline(TestInline1);
  f8 @2 :Inline(TestInline8);
  f16 @3 :Inline(TestInline16);
  f32 @4 :Inline(TestInline32);
  f64 @5 :Inline(TestInline64);
  f128 @6 :Inline(TestInline128);
  f192 @7 :Inline(TestInline192);

  f0p @8 :Inline(TestInline0p);
  f1p @9 :Inline(TestInline1p);
  f8p @10 :Inline(TestInline8p);
  f16p @11 :Inline(TestInline16p);
  f32p @12 :Inline(TestInline32p);
  f64p @13 :Inline(TestInline64p);
  f128p @14 :Inline(TestInline128p);
  f192p @15 :Inline(TestInline192p);

  f1Offset @16 :Inline(TestInline1);
  bit @17 :Bool;
}

struct TestInlineUnions {
  union0 @0 union {
    f0 @4 :Inline(TestInline0);
    f1 @5 :Inline(TestInline1);
    f8 @6 :Inline(TestInline8);
    f16 @7 :Inline(TestInline16);
    f32 @8 :Inline(TestInline32);
    f64 @9 :Inline(TestInline64);
    f128 @10 :Inline(TestInline128);
    f192 @11 :Inline(TestInline192);

    f0p @12 :Inline(TestInline0p);
    f1p @13 :Inline(TestInline1p);
    f8p @14 :Inline(TestInline8p);
    f16p @15 :Inline(TestInline16p);
    f32p @16 :Inline(TestInline32p);
    f64p @17 :Inline(TestInline64p);
    f128p @18 :Inline(TestInline128p);
    f192p @19 :Inline(TestInline192p);
  }

  # Pack one bit in order to make pathological situation for union1.
  bit0 @20: Bool;

  union1 @1 union {
    f0 @21 :Inline(TestInline0);
    f1 @22 :Inline(TestInline1);
    f8 @23 :Inline(TestInline8);
    f16 @24 :Inline(TestInline16);
    f32 @25 :Inline(TestInline32);
    f64 @26 :Inline(TestInline64);
    f128 @27 :Inline(TestInline128);
    f192 @28 :Inline(TestInline192);
  }

  # Fill in the rest of that bitfield from earlier.
  bit2 @29: Bool;
  bit3 @30: Bool;
  bit4 @31: Bool;
  bit5 @32: Bool;
  bit6 @33: Bool;
  bit7 @34: Bool;

  # Interleave two unions to be really annoying.
  union2 @2 union {
    f1p @35 :Inline(TestInline1p);
    f8p @37 :Inline(TestInline8p);
    f16p @40 :Inline(TestInline16p);
    f32p @42 :Inline(TestInline32p);
    f64p @44 :Inline(TestInline64p);
    f128p @46 :Inline(TestInline128p);
    f192p @48 :Inline(TestInline192p);
  }

  union3 @3 union {
    f1p @36 :Inline(TestInline1p);
    f8p @38 :Inline(TestInline8p);
    f16p @41 :Inline(TestInline16p);
    f32p @43 :Inline(TestInline32p);
    f64p @45 :Inline(TestInline64p);
    f128p @47 :Inline(TestInline128p);
    f192p @49 :Inline(TestInline192p);
  }

  byte0 @39: UInt8;
}

struct TestInlineLists {
  voidList      @ 0 : InlineList(Void, 2);
  boolList      @ 1 : InlineList(Bool, 3);
  uInt8List     @ 2 : InlineList(UInt8, 4);
  uInt16List    @ 3 : InlineList(UInt16, 5);
  uInt32List    @ 4 : InlineList(UInt32, 6);
  uInt64List    @ 5 : InlineList(UInt64, 7);
  textList      @ 6 : InlineList(Text, 8);

  structList0   @ 7 : InlineList(TestInline0, 2);
  structList1   @ 8 : InlineList(TestInline1, 3);
  structList8   @ 9 : InlineList(TestInline8, 4);
  structList16  @10 : InlineList(TestInline16, 2);
  structList32  @11 : InlineList(TestInline32, 3);
  structList64  @12 : InlineList(TestInline64, 4);
  structList128 @13 : InlineList(TestInline128, 2);
  structList192 @14 : InlineList(TestInline192, 3);

  structList0p   @15 : InlineList(TestInline0p, 4);
  structList1p   @16 : InlineList(TestInline1p, 2);
  structList8p   @17 : InlineList(TestInline8p, 3);
  structList16p  @18 : InlineList(TestInline16p, 4);
  structList32p  @19 : InlineList(TestInline32p, 2);
  structList64p  @20 : InlineList(TestInline64p, 3);
  structList128p @21 : InlineList(TestInline128p, 4);
  structList192p @22 : InlineList(TestInline192p, 2);
}

struct TestStructLists {
  # Small structs, when encoded as list, will be encoded as primitive lists rather than struct
  # lists, to save space.
  struct Struct0  { f @0 :Void; }
  struct Struct1  { f @0 :Bool; }
  struct Struct8  { f @0 :UInt8; }
  struct Struct16 { f @0 :UInt16; }
  struct Struct32 { f @0 :UInt32; }
  struct Struct64 { f @0 :UInt64; }
  struct StructP  { f @0 :Text; }

  list0  @0 :List(Struct0);
  list1  @1 :List(Struct1);
  list8  @2 :List(Struct8);
  list16 @3 :List(Struct16);
  list32 @4 :List(Struct32);
  list64 @5 :List(Struct64);
  listP  @6 :List(StructP);
}

struct TestInlineDefaults {
  normal @0 :TestInlineLayout = (
      f0 = (f = void),
      f1 = (f = true),
      f8 = (f0 = true, f1 = false, f2 = true),
      f16 = (f0 = 123, f1 = 45),
      f32 = (f0 = 67, f1 = 8901),
      f64 = (f0 = 234, f1 = 567890123),
      f128 = (f0 = 1234567890123, f1 = 4567890123456),
      f192 = (f0 = 7890123456789, f1 = 2345678901234, f2 = 5678901234567),

      f0p = (p0 = "foo"),
      f1p = (f = (f = false), p0 = "bar"),
      f8p = (f = (f0 = true, f1 = true, f2 = false), p0 = "baz"),
      f16p = (f = (f0 = 98, f1 = 76), p0 = "qux", p1 = "quux"),
      f32p = (f = (f0 = 54, f1 = 32109), p0 = "corge", p1 = "grault"),
      f64p = (f = (f0 = 87, f1 = 654321098), p0 = "garply", p1 = "waldo"),
      f128p = (f = (f0 = 7654321098765, f1 = 4321098765432),
               p0 = "fred", p1 = "plugh", p2 = "xyzzy"),
      f192p = (f = (f0 = 1098765432109, f1 = 8765432109876, f2 = 5432109876543),
               p0 = "thud", p1 = "foobar", p2 = "barbaz"));

  unions @1 :TestInlineUnions = (
      union0 = f32(f0 = 67, f1 = 8901),
      union1 = f128(f0 = 1234567890123, f1 = 4567890123456),
      union2 = f1p(p0 = "foo"),
      union3 = f16p(f = (f0 = 98, f1 = 76), p0 = "qux", p1 = "quux"));

  lists @2 :TestInlineLists = (
      voidList      = [void, void],
      boolList      = [false, true, false],
      uInt8List     = [12, 34, 56, 78],
      uInt16List    = [1234, 5678, 9012, 3456, 7890],
      uInt32List    = [123456789, 234567890, 345678901, 456789012, 567890123, 678901234],
      uInt64List    = [1, 2, 3, 4, 5, 6, 7],
      textList      = ["foo", "bar", "baz", "qux", "quux", "corge", "grault", "garply"],

      structList0 = [(f = void), ()],
      structList1 = [(f = true), (f = false), (f = true)],
      structList8 = [(f0 =  true, f1 = false, f2 = false),
                     (f0 = false, f1 =  true, f2 = false),
                     (f0 =  true, f1 =  true, f2 = false),
                     (f0 = false, f1 = false, f2 =  true)],
      structList16 = [(f0 = 12, f1 = 34), (f0 = 56, f1 = 78)],
      structList32 = [(f0 = 90, f1 = 12345), (f0 = 67, f1 = 8901), (f0 = 23, f1 = 45678)],
      structList64 = [(f0 = 90, f1 = 123456789), (f0 = 12, f1 = 345678901),
                      (f0 = 234, f1 = 567890123), (f0 = 45, f1 = 678901234)],
      structList128 = [(f0 = 56789012345678, f1 = 90123456789012),
                       (f0 = 34567890123456, f1 = 78901234567890)],
      structList192 = [(f0 = 1234567890123, f1 = 4567890123456, f2 = 7890123456789),
                       (f0 =  123456789012, f1 = 3456789012345, f2 = 6789012345678),
                       (f0 = 9012345678901, f1 = 2345678901234, f2 = 5678901234567)],

      structList0p = [(f = (f = void), p0 = "foo"), (p0 = "bar"),
                      (f = (), p0 = "baz"), (p0 = "qux")],
      structList1p = [(f = (f = true), p0 = "quux"), (p0 = "corge")],
      structList8p = [(f = (f0 = true), p0 = "grault"), (p0 = "garply"), (p0 = "waldo")],
      structList16p = [(f = (f0 = 123), p0 = "fred", p1 = "plugh"),
                       (p0 = "xyzzy", p1 = "thud"),
                       (p0 = "foobar", p1 = "barbaz"),
                       (p0 = "bazqux", p1 = "quxquux")],
      structList32p = [(f = (f1 = 12345), p0 = "quuxcorge", p1 = "corgegrault"),
                       (p0 = "graultgarply", p1 = "garplywaldo")],
      structList64p = [(f = (f1 = 123456789), p0 = "waldofred", p1 = "fredplugh"),
                       (p0 = "plughxyzzy", p1 = "xyzzythud"),
                       (p0 = "thudfoo", p1 = "foofoo")],
      structList128p = [(f = (f1 = 123456789012345),
                         p0 = "foobaz", p1 = "fooqux", p2 = "foocorge"),
                        (p0 = "barbaz", p1 = "barqux", p2 = "barcorge"),
                        (p0 = "bazbaz", p1 = "bazqux", p2 = "bazcorge"),
                        (p0 = "quxbaz", p1 = "quxqux", p2 = "quxcorge")],
      structList192p = [(f = (f2 = 123456789012345),
                         p0 = "corgebaz", p1 = "corgequx", p2 = "corgecorge"),
                        (p0 = "graultbaz", p1 = "graultqux", p2 = "graultcorge")]);

  structLists @3 :TestStructLists = (
      list0  = [(f = void), (f = void)],
      list1  = [(f = true), (f = false)],
      list8  = [(f = 123), (f = 45)],
      list16 = [(f = 12345), (f = 6789)],
      list32 = [(f = 123456789), (f = 234567890)],
      list64 = [(f = 1234567890123456), (f = 2345678901234567)],
      listP  = [(f = "foo"), (f = "bar")]);
}
