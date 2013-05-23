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

#include "carsales.capnp.h"
#include "capnproto-common.h"

namespace capnproto {
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
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::benchmarkMain<
      capnproto::benchmark::capnp::BenchmarkTypes,
      capnproto::benchmark::capnp::CarSalesTestCase>(argc, argv);
}
