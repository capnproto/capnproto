# Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

using Cxx = import "/capnp/c++.capnp";

@0xff75ddc6a36723c9;
$Cxx.namespace("capnp::benchmark::capnp");

struct ParkingLot {
  cars@0: List(Car);
}

struct TotalValue {
  amount@0: UInt64;
}

struct Car {
  make@0: Text;
  model@1: Text;
  color@2: Color;
  seats@3: UInt8;
  doors@4: UInt8;
  wheels@5: List(Wheel);
  length@6: UInt16;
  width@7: UInt16;
  height@8: UInt16;
  weight@9: UInt32;
  engine@10: Engine;
  fuelCapacity@11: Float32;
  fuelLevel@12: Float32;
  hasPowerWindows@13: Bool;
  hasPowerSteering@14: Bool;
  hasCruiseControl@15: Bool;
  cupHolders@16: UInt8;
  hasNavSystem@17: Bool;
}

enum Color {
  black @0;
  white @1;
  red @2;
  green @3;
  blue @4;
  cyan @5;
  magenta @6;
  yellow @7;
  silver @8;
}

struct Wheel {
  diameter@0: UInt16;
  airPressure@1: Float32;
  snowTires@2: Bool;
}

struct Engine {
  horsepower@0: UInt16;
  cylinders@1: UInt8;
  cc@2: UInt32;
  usesGas@3: Bool;
  usesElectric@4: Bool;
}
