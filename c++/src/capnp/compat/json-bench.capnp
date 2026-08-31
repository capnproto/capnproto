# Copyright (c) 2026 Cap'n Proto contributors
# Licensed under the MIT License:

@0xbeebde01b8e1a9a3;

using Cxx = import "/capnp/c++.capnp";
using Json = import "/capnp/compat/json.capnp";

$Cxx.namespace("capnp::json_bench");

enum Color {
  red @0;
  green @1;
  blue @2;
  ultraviolet @3;
}

struct Small {
  id @0 :UInt32;
  enabled @1 :Bool;
  ratio @2 :Float64;
  name @3 :Text;
  color @4 :Color;
}

# Exercises every JSON representation which the stock codec can produce without
# an application-defined capability or AnyPointer handler.
struct Everything {
  voidField @0 :Void;
  boolField @1 :Bool;
  int8Field @2 :Int8;
  int16Field @3 :Int16;
  int32Field @4 :Int32;
  int64Field @5 :Int64;
  uint8Field @6 :UInt8;
  uint16Field @7 :UInt16;
  uint32Field @8 :UInt32;
  uint64Field @9 :UInt64;
  float32Field @10 :Float32;
  float64Field @11 :Float64;
  textField @12 :Text;
  dataField @13 :Data;
  enumField @14 :Color;
  small @15 :Small;
  boolList @16 :List(Bool);
  intList @17 :List(Int32);
  uint64List @18 :List(UInt64);
  floatList @19 :List(Float64);
  textList @20 :List(Text);
  dataList @21 :List(Data);
  structList @22 :List(Small);
  enumList @23 :List(Color);

  choice :union {
    nothing @24 :Void;
    number @25 :Int32;
    string @26 :Text;
    object @27 :Small;
  }
}

struct Record {
  id @0 :UInt64;
  active @1 :Bool;
  score @2 :Float64;
  name @3 :Text;
  tags @4 :List(Text);
  payload @5 :Data;
  location @6 :Small;
}

struct Document {
  records @0 :List(Record);
  title @1 :Text;
  version @2 :UInt32;
}

# Covers annotation-driven names, flattening, discriminated unions, binary
# encodings, and embedded raw JSON values.
struct Annotated {
  displayName @0 :Text $Json.name("display-name");
  base64Data @1 :Data $Json.base64;
  hexData @2 :Data $Json.hex;

  details :group $Json.flatten(prefix = "detail.") {
    count @3 :UInt32;
    note @4 :Text;
  }

  selection :union $Json.flatten() $Json.discriminator(name = "kind") {
    text @5 :Text;
    amount @6 :Int32;
  }

  raw @7 :Json.Value;
}
