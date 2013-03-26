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

#ifndef CAPNPROTO_BENCHMARK_CAPNPROTO_COMMON_H_
#define CAPNPROTO_BENCHMARK_CAPNPROTO_COMMON_H_

#include "common.h"
#include <capnproto/serialize.h>
#include <capnproto/serialize-snappy.h>
#include <thread>

namespace capnproto {
namespace benchmark {
namespace capnp {

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

#endif  // CAPNPROTO_BENCHMARK_CAPNPROTO_COMMON_H_
