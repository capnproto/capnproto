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
