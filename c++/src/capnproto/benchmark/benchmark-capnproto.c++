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

template <typename TestCase>
void syncClient(int inputFd, int outputFd, uint64_t iters) {
  MallocMessageBuilder builder;
//  StreamFdMessageReader reader(inputFd, ReaderOptions(), InputStrategy::EAGER_WAIT_FOR_READ_NEXT);

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = TestCase::setupRequest(
        builder.initRoot<typename TestCase::Request>());
    writeMessageToFd(outputFd, builder);

//    reader.readNext();
    StreamFdMessageReader reader(inputFd);
    if (!TestCase::checkResponse(reader.getRoot<typename TestCase::Response>(), expected)) {
      throw std::logic_error("Incorrect response.");
    }
  }
}

template <typename TestCase>
void asyncClientSender(int outputFd,
                       ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                       uint64_t iters) {
  MallocMessageBuilder builder;

  for (; iters > 0; --iters) {
    expectations->post(TestCase::setupRequest(builder.initRoot<typename TestCase::Request>()));
    writeMessageToFd(outputFd, builder);
  }
}

template <typename TestCase>
void asyncClientReceiver(int inputFd,
                         ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                         uint64_t iters) {
  StreamFdMessageReader reader(inputFd, ReaderOptions(), InputStrategy::EAGER_WAIT_FOR_READ_NEXT);

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = expectations->next();
    reader.readNext();
    if (!TestCase::checkResponse(reader.getRoot<typename TestCase::Response>(), expected)) {
      throw std::logic_error("Incorrect response.");
    }
  }
}

template <typename TestCase>
void asyncClient(int inputFd, int outputFd, uint64_t iters) {
  ProducerConsumerQueue<typename TestCase::Expectation> expectations;
  std::thread receiverThread(asyncClientReceiver<TestCase>, inputFd, &expectations, iters);
  asyncClientSender<TestCase>(outputFd, &expectations, iters);
  receiverThread.join();
}

template <typename TestCase>
void server(int inputFd, int outputFd, uint64_t iters) {
  StreamFdMessageReader reader(inputFd, ReaderOptions(), InputStrategy::EAGER_WAIT_FOR_READ_NEXT);
  MallocMessageBuilder builder;

  for (; iters > 0; --iters) {
    reader.readNext();
//    StreamFdMessageReader reader(inputFd);
    TestCase::handleRequest(reader.getRoot<typename TestCase::Request>(),
                            builder.initRoot<typename TestCase::Response>());
    writeMessageToFd(outputFd, builder);
  }
}

template <typename TestCase>
void passByObject(uint64_t iters) {
  MallocMessageBuilder requestMessage;
  MallocMessageBuilder responseMessage;

  for (; iters > 0; --iters) {
    auto request = requestMessage.initRoot<typename TestCase::Request>();
    typename TestCase::Expectation expected = TestCase::setupRequest(request);

    auto response = responseMessage.initRoot<typename TestCase::Response>();
    TestCase::handleRequest(request.asReader(), response);

    if (!TestCase::checkResponse(response.asReader(), expected)) {
      throw std::logic_error("Incorrect response.");
    }
  }
}

template <typename TestCase>
void passByBytes(uint64_t iters) {
  MallocMessageBuilder requestBuilder;
  MallocMessageBuilder responseBuilder;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = TestCase::setupRequest(
        requestBuilder.initRoot<typename TestCase::Request>());

    Array<word> requestBytes = messageToFlatArray(requestBuilder);
    FlatArrayMessageReader requestReader(requestBytes.asPtr());
    TestCase::handleRequest(requestReader.getRoot<typename TestCase::Request>(),
                            responseBuilder.initRoot<typename TestCase::Response>());

    Array<word> responseBytes = messageToFlatArray(responseBuilder);
    FlatArrayMessageReader responseReader(responseBytes.asPtr());
    if (!TestCase::checkResponse(responseReader.getRoot<typename TestCase::Response>(), expected)) {
      throw std::logic_error("Incorrect response.");
    }
  }
}

template <typename TestCase, typename Func>
void passByPipe(Func&& clientFunc, uint64_t iters) {
  int clientToServer[2];
  int serverToClient[2];
  if (pipe(clientToServer) < 0) throw OsException(errno);
  if (pipe(serverToClient) < 0) throw OsException(errno);

  pid_t child = fork();
  if (child == 0) {
    // Client.
    close(clientToServer[0]);
    close(serverToClient[1]);

    clientFunc(serverToClient[0], clientToServer[1], iters);
    exit(0);
  } else {
    // Server.
    close(clientToServer[1]);
    close(serverToClient[0]);

    server<TestCase>(clientToServer[0], serverToClient[1], iters);

    int status;
    if (waitpid(child, &status, 0) != child) {
      throw OsException(errno);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      throw std::logic_error("Child exited abnormally.");
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "USAGE:  " << argv[0] << " MODE ITERATION_COUNT" << std::endl;
    return 1;
  }

  uint64_t iters = strtoull(argv[2], nullptr, 0);
  srand(123);

  std::cerr << "Doing " << iters << " iterations..." << std::endl;

  std::string mode = argv[1];
  if (mode == "client") {
    syncClient<ExpressionTestCase>(STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "server") {
    server<ExpressionTestCase>(STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "object") {
    passByObject<ExpressionTestCase>(iters);
  } else if (mode == "bytes") {
    passByBytes<ExpressionTestCase>(iters);
  } else if (mode == "pipe") {
    passByPipe<ExpressionTestCase>(syncClient<ExpressionTestCase>, iters);
  } else if (mode == "pipe-async") {
    passByPipe<ExpressionTestCase>(asyncClient<ExpressionTestCase>, iters);
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    return 1;
  }

  return 0;
}

}  // namespace protobuf
}  // namespace benchmark
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::capnp::main(argc, argv);
}
