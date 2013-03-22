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
#include <capnproto/serialize.h>
#include <capnproto/serialize-snappy.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <iostream>
#include <stdlib.h>
#include <stdexcept>
#include <memory>
#include <thread>
#include <mutex>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>

namespace capnproto {
namespace benchmark {
namespace capnp {

template <typename T>
class ProducerConsumerQueue {
public:
  ProducerConsumerQueue() {
    front = new Node;
    back = front;
    sem_init(&semaphore, 0, 0);
  }

  ~ProducerConsumerQueue() {
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

class OsException: public std::exception {
public:
  OsException(int error): error(error) {}
  ~OsException() noexcept {}

  const char* what() const noexcept override {
    // TODO:  Use strerror_r or whatever for thread-safety.  Ugh.
    return strerror(error);
  }

private:
  int error;
};

// =======================================================================================

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

int32_t makeExpression(Expression::Builder exp, int depth) {
  if (rand() % 8 < depth) {
    exp.setOp(Operation::VALUE);
    exp.setValue(rand() % 128 + 1);
    return exp.getValue();
  } else {
    // TODO:  Operation_MAX or something.
    exp.setOp((Operation)(rand() % (int)Operation::MODULUS + 1));
    int32_t left = makeExpression(exp.initLeft(), depth + 1);
    int32_t right = makeExpression(exp.initRight(), depth + 1);
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
      case Operation::VALUE:
        break;
    }
    throw std::logic_error("Can't get here.");
  }
}

int32_t evaluateExpression(Expression::Reader exp) {
  switch (exp.getOp()) {
    case Operation::VALUE:
      return exp.getValue();
    case Operation::ADD:
      return evaluateExpression(exp.getLeft()) + evaluateExpression(exp.getRight());
    case Operation::SUBTRACT:
      return evaluateExpression(exp.getLeft()) - evaluateExpression(exp.getRight());
    case Operation::MULTIPLY:
      return evaluateExpression(exp.getLeft()) * evaluateExpression(exp.getRight());
    case Operation::DIVIDE:
      return div(evaluateExpression(exp.getLeft()), evaluateExpression(exp.getRight()));
    case Operation::MODULUS: {
      return mod(evaluateExpression(exp.getLeft()), evaluateExpression(exp.getRight()));
    }
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

template <typename Compression>
struct NoScratch {
  struct ScratchSpace {};

  class MessageReader: public Compression::MessageReader {
  public:
    inline MessageReader(int fd, ScratchSpace& scratch)
        : Compression::MessageReader(fd) {}
  };

  class MessageBuilder: public MallocMessageBuilder {
  public:
    inline MessageBuilder(ScratchSpace& scratch): MallocMessageBuilder() {}
  };
};

template <typename Compression, size_t size>
struct UseScratch {
  struct ScratchSpace {
    word words[size];
  };

  class MessageReader: public Compression::MessageReader {
  public:
    inline MessageReader(int fd, ScratchSpace& scratch)
        : Compression::MessageReader(fd, ReaderOptions(), arrayPtr(scratch.words, size)) {}
  };

  class MessageBuilder: public MallocMessageBuilder {
  public:
    inline MessageBuilder(ScratchSpace& scratch)
        : MallocMessageBuilder(arrayPtr(scratch.words, size)) {}
  };
};

// =======================================================================================

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t syncClient(int inputFd, int outputFd, uint64_t iters) {
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
      typename ReuseStrategy::MessageReader reader(inputFd, scratch);
      if (!TestCase::checkResponse(
          reader.template getRoot<typename TestCase::Response>(), expected)) {
        throw std::logic_error("Incorrect response.");
      }
    }
  }

  return output.throughput;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t asyncClientSender(int outputFd,
                           ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
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

template <typename TestCase, typename ReuseStrategy, typename Compression>
void asyncClientReceiver(int inputFd,
                         ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                         uint64_t iters) {
  typename ReuseStrategy::ScratchSpace scratch;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = expectations->next();
    typename ReuseStrategy::MessageReader reader(inputFd, scratch);
    if (!TestCase::checkResponse(
        reader.template getRoot<typename TestCase::Response>(), expected)) {
      throw std::logic_error("Incorrect response.");
    }
  }
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t asyncClient(int inputFd, int outputFd, uint64_t iters) {
  ProducerConsumerQueue<typename TestCase::Expectation> expectations;
  std::thread receiverThread(
      asyncClientReceiver<TestCase, ReuseStrategy, Compression>, inputFd, &expectations, iters);
  uint64_t throughput =
      asyncClientSender<TestCase, ReuseStrategy, Compression>(outputFd, &expectations, iters);
  receiverThread.join();
  return throughput;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t server(int inputFd, int outputFd, uint64_t iters) {
  CountingOutputStream output(outputFd);
  typename ReuseStrategy::ScratchSpace builderScratch;
  typename ReuseStrategy::ScratchSpace readerScratch;

  for (; iters > 0; --iters) {
    typename ReuseStrategy::MessageBuilder builder(builderScratch);
    typename ReuseStrategy::MessageReader reader(inputFd, readerScratch);
    TestCase::handleRequest(reader.template getRoot<typename TestCase::Request>(),
                            builder.template initRoot<typename TestCase::Response>());
    Compression::write(output, builder);
  }

  return output.throughput;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t passByObject(uint64_t iters) {
  typename ReuseStrategy::ScratchSpace requestScratch;
  typename ReuseStrategy::ScratchSpace responseScratch;

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
  }

  return 0;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t passByBytes(uint64_t iters) {
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

template <typename TestCase, typename ReuseStrategy, typename Compression, typename Func>
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

    FdOutputStream(clientToServer[1]).write(&throughput, sizeof(throughput));

    exit(0);
  } else {
    // Server.
    close(clientToServer[1]);
    close(serverToClient[0]);

    uint64_t throughput =
        server<TestCase, ReuseStrategy, Compression>(clientToServer[0], serverToClient[1], iters);

    uint64_t clientThroughput = 0;
    FdInputStream(clientToServer[0]).InputStream::read(&clientThroughput, sizeof(clientThroughput));
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

template <typename ReuseStrategy, typename Compression>
uint64_t doBenchmark(const std::string& mode, uint64_t iters) {
  if (mode == "client") {
    return syncClient<ExpressionTestCase, ReuseStrategy, Compression>(
        STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "server") {
    return server<ExpressionTestCase, ReuseStrategy, Compression>(
        STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "object") {
    return passByObject<ExpressionTestCase, ReuseStrategy, Compression>(iters);
  } else if (mode == "bytes") {
    return passByBytes<ExpressionTestCase, ReuseStrategy, Compression>(iters);
  } else if (mode == "pipe") {
    return passByPipe<ExpressionTestCase, ReuseStrategy, Compression>(
        syncClient<ExpressionTestCase, ReuseStrategy, Compression>, iters);
  } else if (mode == "pipe-async") {
    return passByPipe<ExpressionTestCase, ReuseStrategy, Compression>(
        asyncClient<ExpressionTestCase, ReuseStrategy, Compression>, iters);
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    exit(1);
  }
}

template <typename Compression>
uint64_t doBenchmark2(const std::string& mode, const std::string& reuse, uint64_t iters) {
  if (reuse == "reuse") {
    return doBenchmark<UseScratch<Compression, 1024>, Compression>(mode, iters);
  } else if (reuse == "no-reuse") {
    return doBenchmark<NoScratch<Compression>, Compression>(mode, iters);
  } else {
    std::cerr << "Unknown reuse mode: " << reuse << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 5) {
    std::cerr << "USAGE:  " << argv[0] << " MODE REUSE COMPRESSION ITERATION_COUNT" << std::endl;
    return 1;
  }

  uint64_t iters = strtoull(argv[4], nullptr, 0);
  srand(123);

  std::cerr << "Doing " << iters << " iterations..." << std::endl;

  uint64_t throughput;

  std::string compression = argv[3];
  if (compression == "none") {
    throughput = doBenchmark2<Uncompressed>(argv[1], argv[2], iters);
  } else if (compression == "snappy") {
    throughput = doBenchmark2<SnappyCompressed>(argv[1], argv[2], iters);
  } else {
    std::cerr << "Unknown compression mode: " << compression << std::endl;
    return 1;
  }

  std::cerr << "Average messages size = " << (throughput / iters) << std::endl;

  return 0;
}

}  // namespace protobuf
}  // namespace benchmark
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::capnp::main(argc, argv);
}
