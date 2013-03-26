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

#include "null-common.h"

namespace capnproto {
namespace benchmark {
namespace null {

enum class Color {
  BLACK,
  WHITE,
  RED,
  GREEN,
  BLUE,
  CYAN,
  MAGENTA,
  YELLOW,
  SILVER
};
constexpr uint COLOR_RANGE = static_cast<uint>(Color::SILVER) + 1;

struct Wheel {
  uint16_t diameter;
  float airPressure;
  bool snowTires;
};

struct Engine {
  uint16_t horsepower;
  uint8_t cylinders;
  uint32_t cc;
  bool usesGas;
  bool usesElectric;
};

struct Car {
  const char* make;
  const char* model;
  Color color;
  uint8_t seats;
  uint8_t doors;
  List<Wheel> wheels;
  uint16_t length;
  uint16_t width;
  uint16_t height;
  uint32_t weight;
  Engine engine;
  float fuelCapacity;
  float fuelLevel;
  bool hasPowerWindows;
  bool hasPowerSteering;
  bool hasCruiseControl;
  uint8_t cupHolders;
  bool hasNavSystem;
};


uint64_t carValue(const Car& car) {
  // Do not think too hard about realism.

  uint64_t result = 0;

  result += car.seats * 200;
  result += car.doors * 350;
  for (auto wheel: car.wheels) {
    result += wheel.diameter * wheel.diameter;
    result += wheel.snowTires ? 100 : 0;
  }

  result += car.length * car.width * car.height / 50;

  auto engine = car.engine;
  result += engine.horsepower * 40;
  if (engine.usesElectric) {
    if (engine.usesGas) {
      // hybrid
      result += 5000;
    } else {
      result += 3000;
    }
  }

  result += car.hasPowerWindows ? 100 : 0;
  result += car.hasPowerSteering ? 200 : 0;
  result += car.hasCruiseControl ? 400 : 0;
  result += car.hasNavSystem ? 2000 : 0;

  result += car.cupHolders * 25;

  return result;
}

void randomCar(Car* car) {
  // Do not think too hard about realism.

  static const char* const MAKES[] = { "Toyota", "GM", "Ford", "Honda", "Tesla" };
  static const char* const MODELS[] = { "Camry", "Prius", "Volt", "Accord", "Leaf", "Model S" };

  car->make = copyString(MAKES[fastRand(sizeof(MAKES) / sizeof(MAKES[0]))]);
  car->model = copyString(MODELS[fastRand(sizeof(MODELS) / sizeof(MODELS[0]))]);

  car->color = (Color)fastRand(COLOR_RANGE);
  car->seats = 2 + fastRand(6);
  car->doors = 2 + fastRand(3);

  for (auto& wheel: car->wheels.init(4)) {
    wheel.diameter = 25 + fastRand(15);
    wheel.airPressure = 30 + fastRandDouble(20);
    wheel.snowTires = fastRand(16) == 0;
  }

  car->length = 170 + fastRand(150);
  car->width = 48 + fastRand(36);
  car->height = 54 + fastRand(48);
  car->weight = car->length * car->width * car->height / 200;

  car->engine.horsepower = 100 * fastRand(400);
  car->engine.cylinders = 4 + 2 * fastRand(3);
  car->engine.cc = 800 + fastRand(10000);

  car->fuelCapacity = 10.0 + fastRandDouble(30.0);
  car->fuelLevel = fastRandDouble(car->fuelCapacity);
  car->hasPowerWindows = fastRand(2);
  car->hasPowerSteering = fastRand(2);
  car->hasCruiseControl = fastRand(2);
  car->cupHolders = fastRand(12);
  car->hasNavSystem = fastRand(2);
}

class CarSalesTestCase {
public:
  typedef List<Car> Request;
  typedef uint64_t Response;
  typedef uint64_t Expectation;

  static uint64_t setupRequest(List<Car>* request) {
    uint64_t result = 0;
    for (auto& car: request->init(fastRand(200))) {
      randomCar(&car);
      result += carValue(car);
    }
    return result;
  }
  static void handleRequest(const List<Car>& request, uint64_t* response) {
    *response = 0;
    for (auto& car: request) {
      *response += carValue(car);
    }
  }
  static inline bool checkResponse(uint64_t response, uint64_t expected) {
    return response == expected;
  }
};

}  // namespace null
}  // namespace benchmark
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::benchmarkMain<
      capnproto::benchmark::null::BenchmarkTypes,
      capnproto::benchmark::null::CarSalesTestCase>(argc, argv);
}
