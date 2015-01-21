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

#include "carsales.capnp.h"
#include "capnproto-common.h"

namespace capnp {
namespace benchmark {
namespace capnp {

template <typename ReaderOrBuilder>
uint64_t carValue(ReaderOrBuilder car) {
  // Do not think too hard about realism.

  uint64_t result = 0;

  result += car.getSeats() * 200;
  result += car.getDoors() * 350;
  for (auto wheel: car.getWheels()) {
    result += wheel.getDiameter() * wheel.getDiameter();
    result += wheel.getSnowTires() ? 100 : 0;
  }

  result += car.getLength() * car.getWidth() * car.getHeight() / 50;

  auto engine = car.getEngine();
  result += engine.getHorsepower() * 40;
  if (engine.getUsesElectric()) {
    if (engine.getUsesGas()) {
      // hybrid
      result += 5000;
    } else {
      result += 3000;
    }
  }

  result += car.getHasPowerWindows() ? 100 : 0;
  result += car.getHasPowerSteering() ? 200 : 0;
  result += car.getHasCruiseControl() ? 400 : 0;
  result += car.getHasNavSystem() ? 2000 : 0;

  result += car.getCupHolders() * 25;

  return result;
}

void randomCar(Car::Builder car) {
  // Do not think too hard about realism.

  static const char* const MAKES[] = { "Toyota", "GM", "Ford", "Honda", "Tesla" };
  static const char* const MODELS[] = { "Camry", "Prius", "Volt", "Accord", "Leaf", "Model S" };

  car.setMake(MAKES[fastRand(sizeof(MAKES) / sizeof(MAKES[0]))]);
  car.setModel(MODELS[fastRand(sizeof(MODELS) / sizeof(MODELS[0]))]);

  car.setColor((Color)fastRand((uint)Color::SILVER + 1));
  car.setSeats(2 + fastRand(6));
  car.setDoors(2 + fastRand(3));

  for (auto wheel: car.initWheels(4)) {
    wheel.setDiameter(25 + fastRand(15));
    wheel.setAirPressure(30 + fastRandDouble(20));
    wheel.setSnowTires(fastRand(16) == 0);
  }

  car.setLength(170 + fastRand(150));
  car.setWidth(48 + fastRand(36));
  car.setHeight(54 + fastRand(48));
  car.setWeight(car.getLength() * car.getWidth() * car.getHeight() / 200);

  auto engine = car.initEngine();
  engine.setHorsepower(100 * fastRand(400));
  engine.setCylinders(4 + 2 * fastRand(3));
  engine.setCc(800 + fastRand(10000));
  engine.setUsesGas(true);
  engine.setUsesElectric(fastRand(2));

  car.setFuelCapacity(10.0 + fastRandDouble(30.0));
  car.setFuelLevel(fastRandDouble(car.getFuelCapacity()));
  car.setHasPowerWindows(fastRand(2));
  car.setHasPowerSteering(fastRand(2));
  car.setHasCruiseControl(fastRand(2));
  car.setCupHolders(fastRand(12));
  car.setHasNavSystem(fastRand(2));
}

class CarSalesTestCase {
public:
  typedef ParkingLot Request;
  typedef TotalValue Response;
  typedef uint64_t Expectation;

  static uint64_t setupRequest(ParkingLot::Builder request) {
    uint64_t result = 0;
    for (auto car: request.initCars(fastRand(200))) {
      randomCar(car);
      result += carValue(car);
    }
    return result;
  }
  static void handleRequest(ParkingLot::Reader request, TotalValue::Builder response) {
    uint64_t result = 0;
    for (auto car: request.getCars()) {
      result += carValue(car);
    }
    response.setAmount(result);
  }
  static inline bool checkResponse(TotalValue::Reader response, uint64_t expected) {
    return response.getAmount() == expected;
  }
};

}  // namespace capnp
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::capnp::BenchmarkTypes,
      capnp::benchmark::capnp::CarSalesTestCase>(argc, argv);
}
