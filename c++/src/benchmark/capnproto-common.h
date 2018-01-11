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

#pragma once

#if defined(__GNUC__) && !defined(CAPNP_HEADER_WARNINGS)
#pragma GCC system_header
#endif

#include "common.h"
#include <capnp/serialize.h>
#include <capnp/serialize-packed.h>
#include <kj/debug.h>
#if HAVE_SNAPPY
#include <capnp/serialize-snappy.h>
#endif  // HAVE_SNAPPY
#include <thread>

namespace capnp {
namespace benchmark {
namespace capnp {

class CountingOutputStream: public kj::FdOutputStream {
public:
  CountingOutputStream(int fd): FdOutputStream(fd), throughput(0) {}

  uint64_t throughput;

  void write(const void* buffer, size_t size) override {
    FdOutputStream::write(buffer, size);
    throughput += size;
  }

  void write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    FdOutputStream::write(pieces);
    for (auto& piece: pieces) {
      throughput += piece.size();
    }
  }
};

// =======================================================================================

struct Uncompressed {
  typedef kj::FdInputStream& BufferedInput;
  typedef InputStreamMessageReader MessageReader;

  class ArrayMessageReader: public FlatArrayMessageReader {
  public:
    ArrayMessageReader(kj::ArrayPtr<const byte> array,
                       ReaderOptions options = ReaderOptions(),
                       kj::ArrayPtr<word> scratchSpace = nullptr)
      : FlatArrayMessageReader(kj::arrayPtr(
          reinterpret_cast<const word*>(array.begin()),
          reinterpret_cast<const word*>(array.end())), options) {}
  };

  static inline void write(kj::OutputStream& output, MessageBuilder& builder) {
    writeMessage(output, builder);
  }
};

struct Packed {
  typedef kj::BufferedInputStreamWrapper BufferedInput;
  typedef PackedMessageReader MessageReader;

  class ArrayMessageReader: private kj::ArrayInputStream, public PackedMessageReader {
  public:
    ArrayMessageReader(kj::ArrayPtr<const byte> array,
                       ReaderOptions options = ReaderOptions(),
                       kj::ArrayPtr<word> scratchSpace = nullptr)
      : ArrayInputStream(array),
        PackedMessageReader(*this, options, scratchSpace) {}
  };

  static inline void write(kj::OutputStream& output, MessageBuilder& builder) {
    writePackedMessage(output, builder);
  }

  static inline void write(kj::BufferedOutputStream& output, MessageBuilder& builder) {
    writePackedMessage(output, builder);
  }
};

#if HAVE_SNAPPY
static byte snappyReadBuffer[SNAPPY_BUFFER_SIZE];
static byte snappyWriteBuffer[SNAPPY_BUFFER_SIZE];
static byte snappyCompressedBuffer[SNAPPY_COMPRESSED_BUFFER_SIZE];

struct SnappyCompressed {
  typedef BufferedInputStreamWrapper BufferedInput;
  typedef SnappyPackedMessageReader MessageReader;

  class ArrayMessageReader: private ArrayInputStream, public SnappyPackedMessageReader {
  public:
    ArrayMessageReader(kj::ArrayPtr<const byte> array,
                       ReaderOptions options = ReaderOptions(),
                       kj::ArrayPtr<word> scratchSpace = nullptr)
      : ArrayInputStream(array),
        SnappyPackedMessageReader(static_cast<ArrayInputStream&>(*this), options, scratchSpace,
                                  kj::arrayPtr(snappyReadBuffer, SNAPPY_BUFFER_SIZE)) {}
  };

  static inline void write(OutputStream& output, MessageBuilder& builder) {
    writeSnappyPackedMessage(output, builder,
        kj::arrayPtr(snappyWriteBuffer, SNAPPY_BUFFER_SIZE),
        kj::arrayPtr(snappyCompressedBuffer, SNAPPY_COMPRESSED_BUFFER_SIZE));
  }
};
#endif  // HAVE_SNAPPY

// =======================================================================================

struct NoScratch {
  struct ScratchSpace {};

  template <typename Compression>
  class MessageReader: public Compression::MessageReader {
  public:
    inline MessageReader(typename Compression::BufferedInput& input, ScratchSpace& scratch)
        : Compression::MessageReader(input) {}
  };

  template <typename Compression>
  class ArrayMessageReader: public Compression::ArrayMessageReader {
  public:
    inline ArrayMessageReader(kj::ArrayPtr<const byte> input, ScratchSpace& scratch)
        : Compression::ArrayMessageReader(input) {}
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
word scratchSpace[6 * SCRATCH_SIZE];
int scratchCounter = 0;

struct UseScratch {
  struct ScratchSpace {
    word* words;

    ScratchSpace() {
      KJ_REQUIRE(scratchCounter < 6, "Too many scratch spaces needed at once.");
      words = scratchSpace + scratchCounter++ * SCRATCH_SIZE;
    }
    ~ScratchSpace() noexcept {
      --scratchCounter;
    }
  };

  template <typename Compression>
  class MessageReader: public Compression::MessageReader {
  public:
    inline MessageReader(typename Compression::BufferedInput& input, ScratchSpace& scratch)
        : Compression::MessageReader(
            input, ReaderOptions(), kj::arrayPtr(scratch.words, SCRATCH_SIZE)) {}
  };

  template <typename Compression>
  class ArrayMessageReader: public Compression::ArrayMessageReader {
  public:
    inline ArrayMessageReader(kj::ArrayPtr<const byte> input, ScratchSpace& scratch)
        : Compression::ArrayMessageReader(
            input, ReaderOptions(), kj::arrayPtr(scratch.words, SCRATCH_SIZE)) {}
  };

  class MessageBuilder: public MallocMessageBuilder {
  public:
    inline MessageBuilder(ScratchSpace& scratch)
        : MallocMessageBuilder(kj::arrayPtr(scratch.words, SCRATCH_SIZE)) {}
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
    kj::FdInputStream inputStream(inputFd);
    typename Compression::BufferedInput bufferedInput(inputStream);

    CountingOutputStream output(outputFd);
    typename ReuseStrategy::ScratchSpace builderScratch;
    typename ReuseStrategy::ScratchSpace readerScratch;

    for (; iters > 0; --iters) {
      typename TestCase::Expectation expected;
      {
        typename ReuseStrategy::MessageBuilder builder(builderScratch);
        expected = TestCase::setupRequest(
            builder.template initRoot<typename TestCase::Request>());
        Compression::write(output, builder);
      }

      {
        typename ReuseStrategy::template MessageReader<Compression> reader(
            bufferedInput, readerScratch);
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
    kj::FdInputStream inputStream(inputFd);
    typename Compression::BufferedInput bufferedInput(inputStream);

    typename ReuseStrategy::ScratchSpace scratch;

    for (; iters > 0; --iters) {
      typename TestCase::Expectation expected = expectations->next();
      typename ReuseStrategy::template MessageReader<Compression> reader(bufferedInput, scratch);
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
    kj::FdInputStream inputStream(inputFd);
    typename Compression::BufferedInput bufferedInput(inputStream);

    CountingOutputStream output(outputFd);
    typename ReuseStrategy::ScratchSpace builderScratch;
    typename ReuseStrategy::ScratchSpace readerScratch;

    for (; iters > 0; --iters) {
      typename ReuseStrategy::MessageBuilder builder(builderScratch);
      typename ReuseStrategy::template MessageReader<Compression> reader(
          bufferedInput, readerScratch);
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
    typename ReuseStrategy::ScratchSpace clientRequestScratch;
    UseScratch::ScratchSpace requestBytesScratch;
    typename ReuseStrategy::ScratchSpace serverRequestScratch;
    typename ReuseStrategy::ScratchSpace serverResponseScratch;
    UseScratch::ScratchSpace responseBytesScratch;
    typename ReuseStrategy::ScratchSpace clientResponseScratch;

    for (; iters > 0; --iters) {
      typename ReuseStrategy::MessageBuilder requestBuilder(clientRequestScratch);
      typename TestCase::Expectation expected = TestCase::setupRequest(
          requestBuilder.template initRoot<typename TestCase::Request>());

      kj::ArrayOutputStream requestOutput(kj::arrayPtr(
          reinterpret_cast<byte*>(requestBytesScratch.words), SCRATCH_SIZE * sizeof(word)));
      Compression::write(requestOutput, requestBuilder);
      throughput += requestOutput.getArray().size();
      typename ReuseStrategy::template ArrayMessageReader<Compression> requestReader(
          requestOutput.getArray(), serverRequestScratch);

      typename ReuseStrategy::MessageBuilder responseBuilder(serverResponseScratch);
      TestCase::handleRequest(requestReader.template getRoot<typename TestCase::Request>(),
                              responseBuilder.template initRoot<typename TestCase::Response>());

      kj::ArrayOutputStream responseOutput(
          kj::arrayPtr(reinterpret_cast<byte*>(responseBytesScratch.words),
                       SCRATCH_SIZE * sizeof(word)));
      Compression::write(responseOutput, responseBuilder);
      throughput += responseOutput.getArray().size();
      typename ReuseStrategy::template ArrayMessageReader<Compression> responseReader(
          responseOutput.getArray(), clientResponseScratch);

      if (!TestCase::checkResponse(
          responseReader.template getRoot<typename TestCase::Response>(), expected)) {
        throw std::logic_error("Incorrect response.");
      }
     }

    return throughput;
  }
};

struct BenchmarkTypes {
  typedef capnp::Uncompressed Uncompressed;
  typedef capnp::Packed Packed;
#if HAVE_SNAPPY
  typedef capnp::SnappyCompressed SnappyCompressed;
#endif  // HAVE_SNAPPY

  typedef capnp::UseScratch ReusableResources;
  typedef capnp::NoScratch SingleUseResources;

  template <typename TestCase, typename ReuseStrategy, typename Compression>
  struct BenchmarkMethods: public capnp::BenchmarkMethods<TestCase, ReuseStrategy, Compression> {};
};

}  // namespace capnp
}  // namespace benchmark
}  // namespace capnp
