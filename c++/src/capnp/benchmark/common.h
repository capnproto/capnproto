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

#ifndef CAPNP_BENCHMARK_COMMON_H_
#define CAPNP_BENCHMARK_COMMON_H_

#include <unistd.h>
#include <limits>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <semaphore.h>
#include <algorithm>
#include <stdexcept>
#include <stdio.h>
#include <string.h>

namespace capnp {
namespace benchmark {

static inline uint32_t nextFastRand() {
  static constexpr uint32_t A = 1664525;
  static constexpr uint32_t C = 1013904223;
  static uint32_t state = C;
  state = A * state + C;
  return state;
}

static inline uint32_t fastRand(uint32_t range) {
  return nextFastRand() % range;
}

static inline double fastRandDouble(double range) {
  return nextFastRand() * range / std::numeric_limits<uint32_t>::max();
}

inline int32_t div(int32_t a, int32_t b) {
  if (b == 0) return std::numeric_limits<int32_t>::max();
  // INT_MIN / -1 => SIGFPE.  Who knew?
  if (a == std::numeric_limits<int32_t>::min() && b == -1) {
    return std::numeric_limits<int32_t>::max();
  }
  return a / b;
}

inline int32_t mod(int32_t a, int32_t b) {
  if (b == 0) return std::numeric_limits<int32_t>::max();
  // INT_MIN % -1 => SIGFPE.  Who knew?
  if (a == std::numeric_limits<int32_t>::min() && b == -1) {
    return std::numeric_limits<int32_t>::max();
  }
  return a % b;
}

static const char* const WORDS[] = {
    "foo ", "bar ", "baz ", "qux ", "quux ", "corge ", "grault ", "garply ", "waldo ", "fred ",
    "plugh ", "xyzzy ", "thud "
};
constexpr size_t WORDS_COUNT = sizeof(WORDS) / sizeof(WORDS[0]);

template <typename T>
class ProducerConsumerQueue {
public:
  ProducerConsumerQueue() {
    front = new Node;
    back = front;
    sem_init(&semaphore, 0, 0);
  }

  ~ProducerConsumerQueue() noexcept(false) {
    while (front != nullptr) {
      Node* oldFront = front;
      front = front->next;
      delete oldFront;
    }
    sem_destroy(&semaphore);
  }

  void post(T t) {
    back->next = new Node(t);
    back = back->next;
    sem_post(&semaphore);
  }

  T next() {
    sem_wait(&semaphore);
    Node* oldFront = front;
    front = front->next;
    delete oldFront;
    return front->value;
  }

private:
  struct Node {
    T value;
    Node* next;

    Node(): next(nullptr) {}
    Node(T value): value(value), next(nullptr) {}
  };

  Node* front;  // Last node that has been consumed.
  Node* back;   // Last node in list.
  sem_t semaphore;
};

// TODO(cleanup):  Use SYSCALL(), get rid of this exception class.
class OsException: public std::exception {
public:
  OsException(int error): error(error) {}
  ~OsException() noexcept {}

  const char* what() const noexcept override {
    return strerror(error);
  }

private:
  int error;
};

static void writeAll(int fd, const void* buffer, size_t size) {
  const char* pos = reinterpret_cast<const char*>(buffer);
  while (size > 0) {
    ssize_t n = write(fd, pos, size);
    if (n <= 0) {
      throw OsException(errno);
    }
    pos += n;
    size -= n;
  }
}

static void readAll(int fd, void* buffer, size_t size) {
  char* pos = reinterpret_cast<char*>(buffer);
  while (size > 0) {
    ssize_t n = read(fd, pos, size);
    if (n <= 0) {
      throw OsException(errno);
    }
    pos += n;
    size -= n;
  }
}

template <typename BenchmarkMethods, typename Func>
uint64_t passByPipe(Func&& clientFunc, uint64_t iters) {
  int clientToServer[2];
  int serverToClient[2];
  if (pipe(clientToServer) < 0) throw OsException(errno);
  if (pipe(serverToClient) < 0) throw OsException(errno);

  pid_t child = fork();
  if (child == 0) {
    // Client.
    close(clientToServer[0]);
    close(serverToClient[1]);

    uint64_t throughput = clientFunc(serverToClient[0], clientToServer[1], iters);
    writeAll(clientToServer[1], &throughput, sizeof(throughput));

    exit(0);
  } else {
    // Server.
    close(clientToServer[1]);
    close(serverToClient[0]);

    uint64_t throughput = BenchmarkMethods::server(clientToServer[0], serverToClient[1], iters);

    uint64_t clientThroughput = 0;
    readAll(clientToServer[0], &clientThroughput, sizeof(clientThroughput));
    throughput += clientThroughput;

    int status;
    if (waitpid(child, &status, 0) != child) {
      throw OsException(errno);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      throw std::logic_error("Child exited abnormally.");
    }

    return throughput;
  }
}

template <typename BenchmarkTypes, typename TestCase, typename Reuse, typename Compression>
uint64_t doBenchmark(const std::string& mode, uint64_t iters) {
  typedef typename BenchmarkTypes::template BenchmarkMethods<TestCase, Reuse, Compression>
      BenchmarkMethods;
  if (mode == "client") {
    return BenchmarkMethods::syncClient(STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "server") {
    return BenchmarkMethods::server(STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "object") {
    return BenchmarkMethods::passByObject(iters, false);
  } else if (mode == "object-size") {
    return BenchmarkMethods::passByObject(iters, true);
  } else if (mode == "bytes") {
    return BenchmarkMethods::passByBytes(iters);
  } else if (mode == "pipe") {
    return passByPipe<BenchmarkMethods>(BenchmarkMethods::syncClient, iters);
  } else if (mode == "pipe-async") {
    return passByPipe<BenchmarkMethods>(BenchmarkMethods::asyncClient, iters);
  } else {
    fprintf(stderr, "Unknown mode: %s\n", mode.c_str());
    exit(1);
  }
}

template <typename BenchmarkTypes, typename TestCase, typename Compression>
uint64_t doBenchmark2(const std::string& mode, const std::string& reuse, uint64_t iters) {
  if (reuse == "reuse") {
    return doBenchmark<
        BenchmarkTypes, TestCase, typename BenchmarkTypes::ReusableResources, Compression>(
            mode, iters);
  } else if (reuse == "no-reuse") {
    return doBenchmark<
        BenchmarkTypes, TestCase, typename BenchmarkTypes::SingleUseResources, Compression>(
            mode, iters);
  } else {
    fprintf(stderr, "Unknown reuse mode: %s\n", reuse.c_str());
    exit(1);
  }
}

template <typename BenchmarkTypes, typename TestCase>
uint64_t doBenchmark3(const std::string& mode, const std::string& reuse,
                      const std::string& compression, uint64_t iters) {
  if (compression == "none") {
    return doBenchmark2<BenchmarkTypes, TestCase, typename BenchmarkTypes::Uncompressed>(
        mode, reuse, iters);
  } else if (compression == "packed") {
    return doBenchmark2<BenchmarkTypes, TestCase, typename BenchmarkTypes::Packed>(
        mode, reuse, iters);
#if HAVE_SNAPPY
  } else if (compression == "snappy") {
    return doBenchmark2<BenchmarkTypes, TestCase, typename BenchmarkTypes::SnappyCompressed>(
        mode, reuse, iters);
#endif  // HAVE_SNAPPY
  } else {
    fprintf(stderr, "Unknown compression mode: %s\n", compression.c_str());
    exit(1);
  }
}

template <typename BenchmarkTypes, typename TestCase>
int benchmarkMain(int argc, char* argv[]) {
  if (argc != 5) {
    fprintf(stderr, "USAGE:  %s MODE REUSE COMPRESSION ITERATION_COUNT\n", argv[0]);
    return 1;
  }

  uint64_t iters = strtoull(argv[4], nullptr, 0);
  uint64_t throughput = doBenchmark3<BenchmarkTypes, TestCase>(argv[1], argv[2], argv[3], iters);
  fprintf(stdout, "%llu\n", (long long unsigned int)throughput);

  return 0;
}

}  // namespace capnp
}  // namespace benchmark

#endif  // CAPNP_BENCHMARK_COMMON_H_
