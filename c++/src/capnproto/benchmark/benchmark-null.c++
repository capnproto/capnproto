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

#include <inttypes.h>
#include <iostream>
#include <string>
#include <stddef.h>
#include <limits.h>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <string.h>
#include <limits>
#include "fast-random.h"

namespace capnproto {
namespace benchmark {
namespace null {

uint64_t arena[1024*1024];
uint64_t* arenaPos = arena;

template <typename T>
T* allocate(int count = 1) {
  T* result = reinterpret_cast<T*>(arenaPos);
  arenaPos += (sizeof(T) * count + 7) / 8;
  if (arenaPos > arena + sizeof(arena) / sizeof(arena[0])) {
    throw std::bad_alloc();
  }
  return result;
}

char* copyString(const char* str) {
  size_t len = strlen(str);
  char* result = allocate<char>(len);
  memcpy(result, str, len + 1);
  return result;
}

// =======================================================================================

enum class Operation {
  ADD,
  SUBTRACT,
  MULTIPLY,
  DIVIDE,
  MODULUS
};
uint OPERATION_RANGE = static_cast<uint>(Operation::MODULUS) + 1;

struct Expression {
  Operation op;

  bool leftIsValue;
  bool rightIsValue;

  union {
    int32_t leftValue;
    Expression* leftExpression;
  };

  union {
    int32_t rightValue;
    Expression* rightExpression;
  };
};

inline int32_t div(int32_t a, int32_t b) {
  if (b == 0) return INT_MAX;
  // INT_MIN / -1 => SIGFPE.  Who knew?
  if (a == INT_MIN && b == -1) return INT_MAX;
  return a / b;
}

inline int32_t mod(int32_t a, int32_t b) {
  if (b == 0) return INT_MAX;
  // INT_MIN % -1 => SIGFPE.  Who knew?
  if (a == INT_MIN && b == -1) return INT_MAX;
  return a % b;
}

int32_t makeExpression(Expression* exp, uint depth) {
  exp->op = (Operation)(fastRand(OPERATION_RANGE));

  int32_t left, right;

  if (fastRand(8) < depth) {
    exp->leftIsValue = true;
    left = fastRand(128) + 1;
    exp->leftValue = left;
  } else {
    exp->leftIsValue = false;
    exp->leftExpression = allocate<Expression>();
    left = makeExpression(exp->leftExpression, depth + 1);
  }

  if (fastRand(8) < depth) {
    exp->rightIsValue = true;
    right = fastRand(128) + 1;
    exp->rightValue = right;
  } else {
    exp->rightIsValue = false;
    exp->rightExpression = allocate<Expression>();
    right = makeExpression(exp->rightExpression, depth + 1);
  }

  switch (exp->op) {
    case Operation::ADD:
      return left + right;
    case Operation::SUBTRACT:
      return left - right;
    case Operation::MULTIPLY:
      return left * right;
    case Operation::DIVIDE:
      return div(left, right);
    case Operation::MODULUS:
      return mod(left, right);
  }
  throw std::logic_error("Can't get here.");
}

int32_t evaluateExpression(const Expression& exp) {
  uint32_t left, right;

  if (exp.leftIsValue) {
    left = exp.leftValue;
  } else {
    left = evaluateExpression(*exp.leftExpression);
  }

  if (exp.rightIsValue) {
    right = exp.rightValue;
  } else {
    right = evaluateExpression(*exp.rightExpression);
  }

  switch (exp.op) {
    case Operation::ADD:
      return left + right;
    case Operation::SUBTRACT:
      return left - right;
    case Operation::MULTIPLY:
      return left * right;
    case Operation::DIVIDE:
      return div(left, right);
    case Operation::MODULUS:
      return mod(left, right);
  }
  throw std::logic_error("Can't get here.");
}

class ExpressionTestCase {
public:
  typedef Expression Request;
  typedef int32_t Response;
  typedef int32_t Expectation;

  static inline int32_t setupRequest(Expression* request) {
    return makeExpression(request, 0);
  }
  static inline void handleRequest(const Expression& request, int32_t* response) {
    *response = evaluateExpression(request);
  }
  static inline bool checkResponse(int32_t response, int32_t expected) {
    return response == expected;
  }

  static size_t spaceUsed(const Expression& expression) {
    return sizeof(Expression) +
        (expression.leftExpression == nullptr ? 0 : spaceUsed(*expression.leftExpression)) +
        (expression.rightExpression == nullptr ? 0 : spaceUsed(*expression.rightExpression));
  }
};

// =======================================================================================
// Test case:  Cat Rank
//
// The server receives a list of candidate search results with scores.  It promotes the ones that
// mention "cat" in their snippet and demotes the ones that mention "dog", sorts the results by
// descending score, and returns.
//
// The promotion multiplier is large enough that all the results mentioning "cat" but not "dog"
// should end up at the front ofthe list, which is how we verify the result.

static const char* const WORDS[] = {
    "foo ", "bar ", "baz ", "qux ", "quux ", "corge ", "grault ", "garply ", "waldo ", "fred ",
    "plugh ", "xyzzy ", "thud "
};
constexpr size_t WORDS_COUNT = sizeof(WORDS) / sizeof(WORDS[0]);

template <typename T>
struct List {
  size_t size;
  T* items;

  inline T* begin() const { return items; }
  inline T* end() const { return items + size; }

  inline List<T>& init(size_t size) {
    this->size = size;
    items = allocate<T>(size);
    return *this;
  }
};

struct SearchResult {
  const char* url;
  double score;
  const char* snippet;
};

struct ScoredResult {
  double score;
  const SearchResult* result;

  ScoredResult() = default;
  ScoredResult(double score, const SearchResult* result): score(score), result(result) {}

  inline bool operator<(const ScoredResult& other) const { return score > other.score; }
};

class CatRankTestCase {
public:
  typedef List<SearchResult> Request;
  typedef List<SearchResult> Response;
  typedef int Expectation;

  static int setupRequest(List<SearchResult>* request) {
    int count = fastRand(1000);
    int goodCount = 0;

    request->init(count);
    for (int i = 0; i < count; i++) {
      SearchResult& result = request->items[i];
      result.score = 1000 - i;
      char* pos = reinterpret_cast<char*>(arenaPos);
      result.url = pos;

      strcpy(pos, "http://example.com/");
      pos += strlen("http://example.com/");
      int urlSize = fastRand(100);
      for (int j = 0; j < urlSize; j++) {
        *pos++ = 'a' + fastRand(26);
      }
      *pos++ = '\0';

      // Retroactively allocate the space we used.
      if (allocate<char>(pos - result.url) != result.url) {
        throw std::bad_alloc();
      }

      bool isCat = fastRand(8) == 0;
      bool isDog = fastRand(8) == 0;
      goodCount += isCat && !isDog;

      pos = reinterpret_cast<char*>(arenaPos);
      result.snippet = pos;

      *pos++ = ' ';

      int prefix = fastRand(20);
      for (int j = 0; j < prefix; j++) {
        const char* word = WORDS[fastRand(WORDS_COUNT)];
        size_t len = strlen(word);
        memcpy(pos, word, len);
        pos += len;
      }

      if (isCat) {
        strcpy(pos, "cat ");
        pos += 4;
      }
      if (isDog) {
        strcpy(pos, "dog ");
        pos += 4;
      }

      int suffix = fastRand(20);
      for (int j = 0; j < suffix; j++) {
        const char* word = WORDS[fastRand(WORDS_COUNT)];
        size_t len = strlen(word);
        memcpy(pos, word, len);
        pos += len;
      }
      *pos++ = '\0';

      // Retroactively allocate the space we used.
      if (allocate<char>(pos - result.snippet) != result.snippet) {
        throw std::bad_alloc();
      }
    }

    return goodCount;
  }

  static inline void handleRequest(
      const List<SearchResult>& request, List<SearchResult>* response) {
    std::vector<ScoredResult> scoredResults;
    scoredResults.reserve(request.size);

    for (auto& result: request) {
      double score = result.score;
      if (strstr(result.snippet, " cat ") != nullptr) {
        score *= 10000;
      }
      if (strstr(result.snippet, " dog ") != nullptr) {
        score /= 10000;
      }
      scoredResults.emplace_back(score, &result);
    }

    std::sort(scoredResults.begin(), scoredResults.end());

    response->init(scoredResults.size());
    SearchResult* dst = response->items;
    for (auto& result: scoredResults) {
      dst->url = copyString(result.result->url);
      dst->score = result.score;
      dst->snippet = copyString(result.result->snippet);
      ++dst;
    }
  }

  static inline bool checkResponse(
      const List<SearchResult>& response, int expectedGoodCount) {
    int goodCount = 0;
    for (auto& result: response) {
      if (result.score > 1001) {
        ++goodCount;
      } else {
        break;
      }
    }

    return goodCount == expectedGoodCount;
  }
};

// =======================================================================================
// Test case:  Car Sales
//
// We have a parking lot full of cars and we want to know how much they are worth.

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

// =======================================================================================

struct SingleUseObjects {
  template <typename ObjectType>
  struct Object {
    struct Reusable {};
    struct SingleUse {
      ObjectType value;
      inline SingleUse(Reusable&) {}
    };
  };
};

struct ReusableObjects {
  template <typename ObjectType>
  struct Object {
    typedef ObjectType Reusable;
    struct SingleUse {
      ObjectType& value;
      inline SingleUse(Reusable& reusable): value(reusable) {}
    };
  };
};

// =======================================================================================

template <typename TestCase>
uint64_t passByObject(uint64_t iters) {
  uint64_t throughput = 0;

  for (; iters > 0; --iters) {
    arenaPos = arena;

    typename TestCase::Request request;
    typename TestCase::Expectation expected = TestCase::setupRequest(&request);

    typename TestCase::Response response;
    TestCase::handleRequest(request, &response);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }

    throughput += (arenaPos - arena) * sizeof(arena[0]);
  }

  return throughput;
}

template <typename TestCase>
uint64_t doBenchmark(const std::string& mode, uint64_t iters) {
  if (mode == "object") {
    return passByObject<TestCase>(iters);
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 6) {
    std::cerr << "USAGE:  " << argv[0]
              << " TEST_CASE MODE REUSE COMPRESSION ITERATION_COUNT" << std::endl;
    return 1;
  }

  uint64_t iters = strtoull(argv[5], nullptr, 0);

  uint64_t throughput;

  std::string testcase = argv[1];
  if (testcase == "eval") {
    throughput = doBenchmark<ExpressionTestCase>(argv[2], iters);
  } else if (testcase == "catrank") {
    throughput = doBenchmark<CatRankTestCase>(argv[2], iters);
  } else if (testcase == "carsales") {
    throughput = doBenchmark<CarSalesTestCase>(argv[2], iters);
  } else {
    std::cerr << "Unknown test case: " << testcase << std::endl;
    return 1;
  }

  std::cout << throughput << std::endl;

  return 0;
}

}  // namespace null
}  // namespace benchmark
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::null::main(argc, argv);
}
