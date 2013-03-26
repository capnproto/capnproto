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

#include "benchmark.capnp.h"
#include "benchmark-common.h"
#include <capnproto/serialize.h>
#include <capnproto/serialize-snappy.h>
#include <thread>

namespace capnproto {
namespace benchmark {
namespace capnp {

// =======================================================================================
// Test case:  Expression evaluation

int32_t makeExpression(Expression::Builder exp, uint depth) {
  // TODO:  Operation_RANGE or something.
  exp.setOp((Operation)(fastRand((int)Operation::MODULUS + 1)));

  uint32_t left, right;

  if (fastRand(8) < depth) {
    exp.setLeftIsValue(true);
    left = fastRand(128) + 1;
    exp.setLeftValue(left);
  } else {
    left = makeExpression(exp.initLeftExpression(), depth + 1);
  }

  if (fastRand(8) < depth) {
    exp.setRightIsValue(true);
    right = fastRand(128) + 1;
    exp.setRightValue(right);
  } else {
    right = makeExpression(exp.initRightExpression(), depth + 1);
  }

  switch (exp.getOp()) {
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

int32_t evaluateExpression(Expression::Reader exp) {
  int32_t left, right;

  if (exp.getLeftIsValue()) {
    left = exp.getLeftValue();
  } else {
    left = evaluateExpression(exp.getLeftExpression());
  }

  if (exp.getRightIsValue()) {
    right = exp.getRightValue();
  } else {
    right = evaluateExpression(exp.getRightExpression());
  }

  switch (exp.getOp()) {
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
  ~ExpressionTestCase() {}

  typedef Expression Request;
  typedef EvaluationResult Response;
  typedef int32_t Expectation;

  static inline int32_t setupRequest(Expression::Builder request) {
    return makeExpression(request, 0);
  }
  static inline void handleRequest(Expression::Reader request, EvaluationResult::Builder response) {
    response.setValue(evaluateExpression(request));
  }
  static inline bool checkResponse(EvaluationResult::Reader response, int32_t expected) {
    return response.getValue() == expected;
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

struct ScoredResult {
  double score;
  SearchResult::Reader result;

  ScoredResult() = default;
  ScoredResult(double score, SearchResult::Reader result): score(score), result(result) {}

  inline bool operator<(const ScoredResult& other) const { return score > other.score; }
};

class CatRankTestCase {
public:
  typedef SearchResultList Request;
  typedef SearchResultList Response;
  typedef int Expectation;

  static int setupRequest(SearchResultList::Builder request) {
    int count = fastRand(1000);
    int goodCount = 0;

    auto list = request.initResults(count);

    for (int i = 0; i < count; i++) {
      SearchResult::Builder result = list[i];
      result.setScore(1000 - i);
      int urlSize = fastRand(100);

      static const char URL_PREFIX[] = "http://example.com/";
      auto url = result.initUrl(urlSize + sizeof(URL_PREFIX));

      strcpy(url.data(), URL_PREFIX);
      char* pos = url.data() + strlen(URL_PREFIX);
      for (int j = 0; j < urlSize; j++) {
        *pos++ = 'a' + fastRand(26);
      }

      bool isCat = fastRand(8) == 0;
      bool isDog = fastRand(8) == 0;
      goodCount += isCat && !isDog;

      static std::string snippet;
      snippet.clear();
      snippet.push_back(' ');

      int prefix = fastRand(20);
      for (int j = 0; j < prefix; j++) {
        snippet.append(WORDS[fastRand(WORDS_COUNT)]);
      }

      if (isCat) snippet.append("cat ");
      if (isDog) snippet.append("dog ");

      int suffix = fastRand(20);
      for (int j = 0; j < suffix; j++) {
        snippet.append(WORDS[fastRand(WORDS_COUNT)]);
      }

      result.setSnippet(snippet);
    }

    return goodCount;
  }

  static inline void handleRequest(SearchResultList::Reader request,
                                   SearchResultList::Builder response) {
    std::vector<ScoredResult> scoredResults;

    for (auto result: request.getResults()) {
      double score = result.getScore();
      if (strstr(result.getSnippet().c_str(), " cat ") != nullptr) {
        score *= 10000;
      }
      if (strstr(result.getSnippet().c_str(), " dog ") != nullptr) {
        score /= 10000;
      }
      scoredResults.emplace_back(score, result);
    }

    std::sort(scoredResults.begin(), scoredResults.end());

    auto list = response.initResults(scoredResults.size());
    auto iter = list.begin();
    for (auto result: scoredResults) {
      iter->setScore(result.score);
      iter->setUrl(result.result.getUrl());
      iter->setSnippet(result.result.getSnippet());
      ++iter;
    }
  }

  static inline bool checkResponse(SearchResultList::Reader response, int expectedGoodCount) {
    int goodCount = 0;
    for (auto result: response.getResults()) {
      if (result.getScore() > 1001) {
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

  // TODO: Color_RANGE or something.
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

// =======================================================================================

class CountingOutputStream: public FdOutputStream {
public:
  CountingOutputStream(int fd): FdOutputStream(fd), throughput(0) {}

  uint64_t throughput;

  void write(const void* buffer, size_t size) override {
    FdOutputStream::write(buffer, size);
    throughput += size;
  }

  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    FdOutputStream::write(pieces);
    for (auto& piece: pieces) {
      throughput += piece.size();
    }
  }
};

// =======================================================================================

struct Uncompressed {
  typedef StreamFdMessageReader MessageReader;

  static inline void write(OutputStream& output, MessageBuilder& builder) {
    writeMessage(output, builder);
  }
};

struct SnappyCompressed {
  typedef SnappyFdMessageReader MessageReader;

  static inline void write(OutputStream& output, MessageBuilder& builder) {
    writeSnappyMessage(output, builder);
  }
};

// =======================================================================================

struct NoScratch {
  struct ScratchSpace {};

  template <typename Compression>
  class MessageReader: public Compression::MessageReader {
  public:
    inline MessageReader(int fd, ScratchSpace& scratch)
        : Compression::MessageReader(fd) {}
  };

  class MessageBuilder: public MallocMessageBuilder {
  public:
    inline MessageBuilder(ScratchSpace& scratch): MallocMessageBuilder() {}
  };

  class ObjectSizeCounter {
  public:
    ObjectSizeCounter(uint64_t iters): counter(0) {}

    template <typename RequestBuilder, typename ResponseBuilder>
    void add(RequestBuilder& request, ResponseBuilder& response) {
      for (auto segment: request.getSegmentsForOutput()) {
        counter += segment.size() * sizeof(word);
      }
      for (auto segment: response.getSegmentsForOutput()) {
        counter += segment.size() * sizeof(word);
      }
    }

    uint64_t get() { return counter; }

  private:
    uint64_t counter;
  };
};

constexpr size_t SCRATCH_SIZE = 128 * 1024;
word scratchSpace[4 * SCRATCH_SIZE];
int scratchCounter = 0;

struct UseScratch {
  struct ScratchSpace {
    word* words;

    ScratchSpace() {
      CAPNPROTO_ASSERT(scratchCounter < 4, "Too many scratch spaces needed at once.");
      words = scratchSpace + scratchCounter++ * SCRATCH_SIZE;
    }
    ~ScratchSpace() {
      --scratchCounter;
    }
  };

  template <typename Compression>
  class MessageReader: public Compression::MessageReader {
  public:
    inline MessageReader(int fd, ScratchSpace& scratch)
        : Compression::MessageReader(fd, ReaderOptions(), arrayPtr(scratch.words, SCRATCH_SIZE)) {}
  };

  class MessageBuilder: public MallocMessageBuilder {
  public:
    inline MessageBuilder(ScratchSpace& scratch)
        : MallocMessageBuilder(arrayPtr(scratch.words, SCRATCH_SIZE)) {}
  };

  class ObjectSizeCounter {
  public:
    ObjectSizeCounter(uint64_t iters): iters(iters), maxSize(0) {}

    template <typename RequestBuilder, typename ResponseBuilder>
    void add(RequestBuilder& request, ResponseBuilder& response) {
      size_t counter = 0;
      for (auto segment: request.getSegmentsForOutput()) {
        counter += segment.size() * sizeof(word);
      }
      for (auto segment: response.getSegmentsForOutput()) {
        counter += segment.size() * sizeof(word);
      }
      maxSize = std::max(counter, maxSize);
    }

    uint64_t get() { return iters * maxSize; }

  private:
    uint64_t iters;
    size_t maxSize;
  };
};

// =======================================================================================

template <typename TestCase, typename ReuseStrategy, typename Compression>
struct BenchmarkMethods {
  static uint64_t syncClient(int inputFd, int outputFd, uint64_t iters) {
    CountingOutputStream output(outputFd);
    typename ReuseStrategy::ScratchSpace scratch;

    for (; iters > 0; --iters) {
      typename TestCase::Expectation expected;
      {
        typename ReuseStrategy::MessageBuilder builder(scratch);
        expected = TestCase::setupRequest(
            builder.template initRoot<typename TestCase::Request>());
        Compression::write(output, builder);
      }

      {
        typename ReuseStrategy::template MessageReader<Compression> reader(inputFd, scratch);
        if (!TestCase::checkResponse(
            reader.template getRoot<typename TestCase::Response>(), expected)) {
          throw std::logic_error("Incorrect response.");
        }
      }
    }

    return output.throughput;
  }

  static uint64_t asyncClientSender(
      int outputFd, ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
      uint64_t iters) {
    CountingOutputStream output(outputFd);
    typename ReuseStrategy::ScratchSpace scratch;

    for (; iters > 0; --iters) {
      typename ReuseStrategy::MessageBuilder builder(scratch);
      expectations->post(TestCase::setupRequest(
          builder.template initRoot<typename TestCase::Request>()));
      Compression::write(output, builder);
    }

    return output.throughput;
  }

  static void asyncClientReceiver(
      int inputFd, ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
      uint64_t iters) {
    typename ReuseStrategy::ScratchSpace scratch;

    for (; iters > 0; --iters) {
      typename TestCase::Expectation expected = expectations->next();
      typename ReuseStrategy::template MessageReader<Compression> reader(inputFd, scratch);
      if (!TestCase::checkResponse(
          reader.template getRoot<typename TestCase::Response>(), expected)) {
        throw std::logic_error("Incorrect response.");
      }
    }
  }

  static uint64_t asyncClient(int inputFd, int outputFd, uint64_t iters) {
    ProducerConsumerQueue<typename TestCase::Expectation> expectations;
    std::thread receiverThread(asyncClientReceiver, inputFd, &expectations, iters);
    uint64_t throughput = asyncClientSender(outputFd, &expectations, iters);
    receiverThread.join();
    return throughput;
  }

  static uint64_t server(int inputFd, int outputFd, uint64_t iters) {
    CountingOutputStream output(outputFd);
    typename ReuseStrategy::ScratchSpace builderScratch;
    typename ReuseStrategy::ScratchSpace readerScratch;

    for (; iters > 0; --iters) {
      typename ReuseStrategy::MessageBuilder builder(builderScratch);
      typename ReuseStrategy::template MessageReader<Compression> reader(inputFd, readerScratch);
      TestCase::handleRequest(reader.template getRoot<typename TestCase::Request>(),
                              builder.template initRoot<typename TestCase::Response>());
      Compression::write(output, builder);
    }

    return output.throughput;
  }

  static uint64_t passByObject(uint64_t iters, bool countObjectSize) {
    typename ReuseStrategy::ScratchSpace requestScratch;
    typename ReuseStrategy::ScratchSpace responseScratch;

    typename ReuseStrategy::ObjectSizeCounter counter(iters);

    for (; iters > 0; --iters) {
      typename ReuseStrategy::MessageBuilder requestMessage(requestScratch);
      auto request = requestMessage.template initRoot<typename TestCase::Request>();
      typename TestCase::Expectation expected = TestCase::setupRequest(request);

      typename ReuseStrategy::MessageBuilder responseMessage(responseScratch);
      auto response = responseMessage.template initRoot<typename TestCase::Response>();
      TestCase::handleRequest(request.asReader(), response);

      if (!TestCase::checkResponse(response.asReader(), expected)) {
        throw std::logic_error("Incorrect response.");
      }

      if (countObjectSize) {
        counter.add(requestMessage, responseMessage);
      }
    }

    return counter.get();
  }

  static uint64_t passByBytes(uint64_t iters) {
    uint64_t throughput = 0;
    typename ReuseStrategy::ScratchSpace requestScratch;
    typename ReuseStrategy::ScratchSpace responseScratch;

    for (; iters > 0; --iters) {
      typename ReuseStrategy::MessageBuilder requestBuilder(requestScratch);
      typename TestCase::Expectation expected = TestCase::setupRequest(
          requestBuilder.template initRoot<typename TestCase::Request>());

      Array<word> requestBytes = messageToFlatArray(requestBuilder);
      throughput += requestBytes.size() * sizeof(word);
      FlatArrayMessageReader requestReader(requestBytes.asPtr());
      typename ReuseStrategy::MessageBuilder responseBuilder(responseScratch);
      TestCase::handleRequest(requestReader.template getRoot<typename TestCase::Request>(),
                              responseBuilder.template initRoot<typename TestCase::Response>());

      Array<word> responseBytes = messageToFlatArray(responseBuilder);
      throughput += responseBytes.size() * sizeof(word);
      FlatArrayMessageReader responseReader(responseBytes.asPtr());
      if (!TestCase::checkResponse(
          responseReader.template getRoot<typename TestCase::Response>(), expected)) {
        throw std::logic_error("Incorrect response.");
      }
     }

    return throughput;
  }
};

struct BenchmarkTypes {
  typedef capnp::ExpressionTestCase ExpressionTestCase;
  typedef capnp::CatRankTestCase CatRankTestCase;
  typedef capnp::CarSalesTestCase CarSalesTestCase;

  typedef capnp::SnappyCompressed SnappyCompressed;
  typedef capnp::Uncompressed Uncompressed;

  typedef capnp::UseScratch ReusableResources;
  typedef capnp::NoScratch SingleUseResources;

  template <typename TestCase, typename ReuseStrategy, typename Compression>
  struct BenchmarkMethods: public capnp::BenchmarkMethods<TestCase, ReuseStrategy, Compression> {};
};

}  // namespace capnp
}  // namespace benchmark
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::benchmarkMain<
      capnproto::benchmark::capnp::BenchmarkTypes>(argc, argv);
}
