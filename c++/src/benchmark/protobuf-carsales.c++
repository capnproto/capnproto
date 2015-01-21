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

#include "carsales.pb.h"
#include "protobuf-common.h"

namespace capnp {
namespace benchmark {
namespace protobuf {

uint64_t carValue(const Car& car) {
  // Do not think too hard about realism.

  uint64_t result = 0;

  result += car.seats() * 200;
  result += car.doors() * 350;
  for (auto& wheel: car.wheel()) {
    result += wheel.diameter() * wheel.diameter();
    result += wheel.snow_tires() ? 100 : 0;
  }

  result += car.length() * car.width() * car.height() / 50;

  const Engine& engine = car.engine();
  result += engine.horsepower() * 40;
  if (engine.uses_electric()) {
    if (engine.uses_gas()) {
      // hybrid
      result += 5000;
    } else {
      result += 3000;
    }
  }

  result += car.has_power_windows() ? 100 : 0;
  result += car.has_power_steering() ? 200 : 0;
  result += car.has_cruise_control() ? 400 : 0;
  result += car.has_nav_system() ? 2000 : 0;

  result += car.cup_holders() * 25;

  return result;
}

void randomCar(Car* car) {
  // Do not think too hard about realism.

  static const char* const MAKES[] = { "Toyota", "GM", "Ford", "Honda", "Tesla" };
  static const char* const MODELS[] = { "Camry", "Prius", "Volt", "Accord", "Leaf", "Model S" };

  car->set_make(MAKES[fastRand(sizeof(MAKES) / sizeof(MAKES[0]))]);
  car->set_model(MODELS[fastRand(sizeof(MODELS) / sizeof(MODELS[0]))]);

  car->set_color((Color)fastRand(Color_MAX));
  car->set_seats(2 + fastRand(6));
  car->set_doors(2 + fastRand(3));

  for (uint i = 0; i < 4; i++) {
    Wheel* wheel = car->add_wheel();
    wheel->set_diameter(25 + fastRand(15));
    wheel->set_air_pressure(30 + fastRandDouble(20));
    wheel->set_snow_tires(fastRand(16) == 0);
  }

  car->set_length(170 + fastRand(150));
  car->set_width(48 + fastRand(36));
  car->set_height(54 + fastRand(48));
  car->set_weight(car->length() * car->width() * car->height() / 200);

  Engine* engine = car->mutable_engine();
  engine->set_horsepower(100 * fastRand(400));
  engine->set_cylinders(4 + 2 * fastRand(3));
  engine->set_cc(800 + fastRand(10000));
  engine->set_uses_gas(true);
  engine->set_uses_electric(fastRand(2));

  car->set_fuel_capacity(10.0 + fastRandDouble(30.0));
  car->set_fuel_level(fastRandDouble(car->fuel_capacity()));
  car->set_has_power_windows(fastRand(2));
  car->set_has_power_steering(fastRand(2));
  car->set_has_cruise_control(fastRand(2));
  car->set_cup_holders(fastRand(12));
  car->set_has_nav_system(fastRand(2));
}

class CarSalesTestCase {
public:
  typedef ParkingLot Request;
  typedef TotalValue Response;
  typedef uint64_t Expectation;

  static uint64_t setupRequest(ParkingLot* request) {
    uint count = fastRand(200);
    uint64_t result = 0;
    for (uint i = 0; i < count; i++) {
      Car* car = request->add_car();
      randomCar(car);
      result += carValue(*car);
    }
    return result;
  }
  static void handleRequest(const ParkingLot& request, TotalValue* response) {
    uint64_t result = 0;
    for (auto& car: request.car()) {
      result += carValue(car);
    }
    response->set_amount(result);
  }
  static inline bool checkResponse(const TotalValue& response, uint64_t expected) {
    return response.amount() == expected;
  }
};

}  // namespace protobuf
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::protobuf::BenchmarkTypes,
      capnp::benchmark::protobuf::CarSalesTestCase>(argc, argv);
}
