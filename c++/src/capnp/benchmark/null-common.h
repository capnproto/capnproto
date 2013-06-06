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

#include "common.h"

namespace capnp {
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

// =======================================================================================

struct SingleUseObjects {
  class ObjectSizeCounter {
  public:
    ObjectSizeCounter(uint64_t iters): counter(0) {}

    void add(uint64_t wordCount) {
      counter += wordCount;
    }

    uint64_t get() { return counter; }

  private:
    uint64_t counter;
  };
};

struct ReusableObjects {
  class ObjectSizeCounter {
  public:
    ObjectSizeCounter(uint64_t iters): iters(iters), maxSize(0) {}

    void add(size_t wordCount) {
      maxSize = std::max(wordCount, maxSize);
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
    fprintf(stderr, "Null benchmark doesn't do I/O.\n");
    exit(1);
  }

  static uint64_t asyncClientSender(
      int outputFd, ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
      uint64_t iters) {
    fprintf(stderr, "Null benchmark doesn't do I/O.\n");
    exit(1);
  }

  static void asyncClientReceiver(
      int inputFd, ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
      uint64_t iters) {
    fprintf(stderr, "Null benchmark doesn't do I/O.\n");
    exit(1);
  }

  static uint64_t asyncClient(int inputFd, int outputFd, uint64_t iters) {
    fprintf(stderr, "Null benchmark doesn't do I/O.\n");
    exit(1);
  }

  static uint64_t server(int inputFd, int outputFd, uint64_t iters) {
    fprintf(stderr, "Null benchmark doesn't do I/O.\n");
    exit(1);
  }

  static uint64_t passByObject(uint64_t iters, bool countObjectSize) {
    typename ReuseStrategy::ObjectSizeCounter sizeCounter(iters);

    for (; iters > 0; --iters) {
      arenaPos = arena;

      typename TestCase::Request request;
      typename TestCase::Expectation expected = TestCase::setupRequest(&request);

      typename TestCase::Response response;
      TestCase::handleRequest(request, &response);
      if (!TestCase::checkResponse(response, expected)) {
        throw std::logic_error("Incorrect response.");
      }

      sizeCounter.add((arenaPos - arena) * sizeof(arena[0]));
    }

    return sizeCounter.get();
  }

  static uint64_t passByBytes(uint64_t iters) {
    fprintf(stderr, "Null benchmark doesn't do I/O.\n");
    exit(1);
  }
};

struct BenchmarkTypes {
  typedef void Uncompressed;
  typedef void Packed;
#if HAVE_SNAPPY
  typedef void SnappyCompressed;
#endif  // HAVE_SNAPPY

  typedef ReusableObjects ReusableResources;
  typedef SingleUseObjects SingleUseResources;

  template <typename TestCase, typename ReuseStrategy, typename Compression>
  struct BenchmarkMethods: public null::BenchmarkMethods<TestCase, ReuseStrategy, Compression> {};
};

}  // namespace null
}  // namespace benchmark
}  // namespace capnp
