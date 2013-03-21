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

#include "benchmark.pb.h"
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
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
namespace protobuf {

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

int32_t makeExpression(Expression* exp, int depth) {
  if (rand() % 8 < depth) {
    exp->set_op(Operation::VALUE);
    exp->set_value(rand() % 128 + 1);
    return exp->value();
  } else {
    exp->set_op((Operation)(rand() % Operation_MAX + 1));
    int32_t left = makeExpression(exp->mutable_left(), depth + 1);
    int32_t right = makeExpression(exp->mutable_right(), depth + 1);
    switch (exp->op()) {
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

int32_t evaluateExpression(const Expression& exp) {
  switch (exp.op()) {
    case Operation::VALUE:
      return exp.value();
    case Operation::ADD:
      return evaluateExpression(exp.left()) + evaluateExpression(exp.right());
    case Operation::SUBTRACT:
      return evaluateExpression(exp.left()) - evaluateExpression(exp.right());
    case Operation::MULTIPLY:
      return evaluateExpression(exp.left()) * evaluateExpression(exp.right());
    case Operation::DIVIDE:
      return div(evaluateExpression(exp.left()), evaluateExpression(exp.right()));
    case Operation::MODULUS: {
      return mod(evaluateExpression(exp.left()), evaluateExpression(exp.right()));
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

  static inline int32_t setupRequest(Expression* request) {
    return makeExpression(request, 0);
  }
  static inline void handleRequest(const Expression& request, EvaluationResult* response) {
    response->set_value(evaluateExpression(request));
  }
  static inline bool checkResponse(const EvaluationResult& response, int32_t expected) {
    return response.value() == expected;
  }
};

// =======================================================================================

void writeProtoFast(const google::protobuf::MessageLite& message,
                    google::protobuf::io::FileOutputStream* rawOutput) {
  google::protobuf::io::CodedOutputStream output(rawOutput);
  const int size = message.ByteSize();
  output.WriteVarint32(size);
  uint8_t* buffer = output.GetDirectBufferForNBytesAndAdvance(size);
  if (buffer != NULL) {
    message.SerializeWithCachedSizesToArray(buffer);
  } else {
    message.SerializeWithCachedSizes(&output);
    if (output.HadError()) {
      throw OsException(rawOutput->GetErrno());
    }
  }
}

void readProtoFast(google::protobuf::io::ZeroCopyInputStream* rawInput,
                   google::protobuf::MessageLite* message) {
  google::protobuf::io::CodedInputStream input(rawInput);
  uint32_t size;
  if (!input.ReadVarint32(&size)) {
    throw std::logic_error("Read failed.");
  }

  auto limit = input.PushLimit(size);

  if (!message->MergePartialFromCodedStream(&input) ||
      !input.ConsumedEntireMessage()) {
    throw std::logic_error("Read failed.");
  }

  input.PopLimit(limit);
}

template <typename TestCase>
void syncClient(int inputFd, int outputFd, uint64_t iters) {
  google::protobuf::io::FileOutputStream output(outputFd);
  google::protobuf::io::FileInputStream input(inputFd);

  typename TestCase::Request request;
  typename TestCase::Response response;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = TestCase::setupRequest(&request);
    writeProtoFast(request, &output);
    if (!output.Flush()) throw OsException(output.GetErrno());
    request.Clear();

    // std::cerr << "client: wait" << std::endl;
    readProtoFast(&input, &response);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    response.Clear();
  }
}

template <typename TestCase>
void asyncClientSender(int outputFd,
                       ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                       uint64_t iters) {
  google::protobuf::io::FileOutputStream output(outputFd);
  typename TestCase::Request request;

  for (; iters > 0; --iters) {
    expectations->post(TestCase::setupRequest(&request));
    writeProtoFast(request, &output);
    request.Clear();
  }

  if (!output.Flush()) throw OsException(output.GetErrno());
}

template <typename TestCase>
void asyncClientReceiver(int inputFd,
                         ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                         uint64_t iters) {
  google::protobuf::io::FileInputStream input(inputFd);
  typename TestCase::Response response;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = expectations->next();
    readProtoFast(&input, &response);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    response.Clear();
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
  google::protobuf::io::FileOutputStream output(outputFd);
  google::protobuf::io::FileInputStream input(inputFd);

  typename TestCase::Request request;
  typename TestCase::Response response;

  for (; iters > 0; --iters) {
    readProtoFast(&input, &request);
    TestCase::handleRequest(request, &response);
    request.Clear();

    writeProtoFast(response, &output);
    if (!output.Flush()) throw std::logic_error("Write failed.");
    response.Clear();
  }
}

template <typename TestCase>
void passByObject(uint64_t iters) {
  typename TestCase::Request request;
  typename TestCase::Response response;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = TestCase::setupRequest(&request);
    TestCase::handleRequest(request, &response);
    request.Clear();
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    response.Clear();
  }
}

template <typename TestCase>
void passByBytes(uint64_t iters) {
  typename TestCase::Request clientRequest;
  typename TestCase::Request serverRequest;
  typename TestCase::Response serverResponse;
  typename TestCase::Response clientResponse;
  std::string requestString, responseString;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = TestCase::setupRequest(&clientRequest);

    clientRequest.SerializePartialToString(&requestString);
    clientRequest.Clear();

    serverRequest.ParsePartialFromString(requestString);
    requestString.clear();

    TestCase::handleRequest(serverRequest, &serverResponse);
    serverRequest.Clear();

    serverResponse.SerializePartialToString(&responseString);
    serverResponse.Clear();

    clientResponse.ParsePartialFromString(responseString);
    responseString.clear();

    if (!TestCase::checkResponse(clientResponse, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    clientResponse.Clear();
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
  return capnproto::benchmark::protobuf::main(argc, argv);
}
