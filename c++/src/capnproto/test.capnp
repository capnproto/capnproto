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
  interfaceList @33 : Void;  # TODO
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
  float32List   @27 : List(Float32) = [5555.5, 2222.25];
  float64List   @28 : List(Float64) = [7777.75, 1111.125];
  textList      @29 : List(Text)    = ["plugh", "xyzzy", "thud"];
  dataList      @30 : List(Data)    = ["oops", "exhausted", "rfc3092"];
  structList    @31 : List(TestAllTypes) = [
      (textField = "structlist 1"),
      (textField = "structlist 2"),
      (textField = "structlist 3")];
  enumList      @32 : List(TestEnum) = [foo, garply];
  interfaceList @33 : List(Void);  # TODO
}
