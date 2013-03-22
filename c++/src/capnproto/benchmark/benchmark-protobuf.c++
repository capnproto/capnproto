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

struct SingleUseMessages {
  template <typename MessageType>
  struct Message {
    struct Reusable {};
    struct SingleUse: public MessageType {
      inline SingleUse(Reusable&) {}
    };
  };

  struct ReusableString {};
  struct SingleUseString: std::string {
    inline SingleUseString(ReusableString&) {}
  };

  template <typename MessageType>
  static inline void doneWith(MessageType& message) {
    // Don't clear -- single-use.
  }
};

struct ReusableMessages {
  template <typename MessageType>
  struct Message {
    struct Reusable: public MessageType {};
    typedef MessageType& SingleUse;
  };

  typedef std::string ReusableString;
  typedef std::string& SingleUseString;

  template <typename MessageType>
  static inline void doneWith(MessageType& message) {
    message.Clear();
  }
};

// =======================================================================================
// The protobuf Java library defines a format for writing multiple protobufs to a stream, in which
// each message is prefixed by a varint size.  This was never added to the C++ library.  It's easy
// to do naively, but tricky to implement without accidentally losing various optimizations.  These
// two functions should be optimal.

void writeDelimited(const google::protobuf::MessageLite& message,
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

void readDelimited(google::protobuf::io::ZeroCopyInputStream* rawInput,
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

// =======================================================================================

#define REUSABLE(type) \
  typename ReuseStrategy::template Message<typename TestCase::type>::Reusable
#define SINGLE_USE(type) \
  typename ReuseStrategy::template Message<typename TestCase::type>::SingleUse

template <typename TestCase, typename ReuseStrategy>
void syncClient(int inputFd, int outputFd, uint64_t iters) {
  google::protobuf::io::FileOutputStream output(outputFd);
  google::protobuf::io::FileInputStream input(inputFd);

  REUSABLE(Request) reusableRequest;
  REUSABLE(Response) reusableResponse;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) request(reusableRequest);
    typename TestCase::Expectation expected = TestCase::setupRequest(&request);
    writeDelimited(request, &output);
    if (!output.Flush()) throw OsException(output.GetErrno());
    ReuseStrategy::doneWith(request);

    SINGLE_USE(Response) response(reusableResponse);
    readDelimited(&input, &response);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    ReuseStrategy::doneWith(response);
  }
}

template <typename TestCase, typename ReuseStrategy>
void asyncClientSender(int outputFd,
                       ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                       uint64_t iters) {
  google::protobuf::io::FileOutputStream output(outputFd);
  REUSABLE(Request) reusableRequest;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) request(reusableRequest);
    expectations->post(TestCase::setupRequest(&request));
    writeDelimited(request, &output);
    ReuseStrategy::doneWith(request);
  }

  if (!output.Flush()) throw OsException(output.GetErrno());
}

template <typename TestCase, typename ReuseStrategy>
void asyncClientReceiver(int inputFd,
                         ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                         uint64_t iters) {
  google::protobuf::io::FileInputStream input(inputFd);
  REUSABLE(Response) reusableResponse;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = expectations->next();
    SINGLE_USE(Response) response(reusableResponse);
    readDelimited(&input, &response);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    ReuseStrategy::doneWith(response);
  }
}

template <typename TestCase, typename ReuseStrategy>
void asyncClient(int inputFd, int outputFd, uint64_t iters) {
  ProducerConsumerQueue<typename TestCase::Expectation> expectations;
  std::thread receiverThread(
      asyncClientReceiver<TestCase, ReuseStrategy>, inputFd, &expectations, iters);
  asyncClientSender<TestCase, ReuseStrategy>(outputFd, &expectations, iters);
  receiverThread.join();
}

template <typename TestCase, typename ReuseStrategy>
void server(int inputFd, int outputFd, uint64_t iters) {
  google::protobuf::io::FileOutputStream output(outputFd);
  google::protobuf::io::FileInputStream input(inputFd);

  REUSABLE(Request) reusableRequest;
  REUSABLE(Response) reusableResponse;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) request(reusableRequest);
    readDelimited(&input, &request);

    SINGLE_USE(Response) response(reusableResponse);
    TestCase::handleRequest(request, &response);
    ReuseStrategy::doneWith(request);

    writeDelimited(response, &output);
    if (!output.Flush()) throw std::logic_error("Write failed.");
    ReuseStrategy::doneWith(response);
  }
}

template <typename TestCase, typename ReuseStrategy>
void passByObject(uint64_t iters) {
  REUSABLE(Request) reusableRequest;
  REUSABLE(Response) reusableResponse;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) request(reusableRequest);
    typename TestCase::Expectation expected = TestCase::setupRequest(&request);

    SINGLE_USE(Response) response(reusableResponse);
    TestCase::handleRequest(request, &response);
    ReuseStrategy::doneWith(request);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    ReuseStrategy::doneWith(response);
  }
}

template <typename TestCase, typename ReuseStrategy>
void passByBytes(uint64_t iters) {
  REUSABLE(Request) reusableClientRequest;
  REUSABLE(Request) reusableServerRequest;
  REUSABLE(Response) reusableServerResponse;
  REUSABLE(Response) reusableClientResponse;
  typename ReuseStrategy::ReusableString reusableRequestString, reusableResponseString;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) clientRequest(reusableClientRequest);
    typename TestCase::Expectation expected = TestCase::setupRequest(&clientRequest);

    typename ReuseStrategy::SingleUseString requestString(reusableRequestString);
    clientRequest.SerializePartialToString(&requestString);
    ReuseStrategy::doneWith(clientRequest);

    SINGLE_USE(Request) serverRequest(reusableServerRequest);
    serverRequest.ParsePartialFromString(requestString);

    SINGLE_USE(Response) serverResponse(reusableServerResponse);
    TestCase::handleRequest(serverRequest, &serverResponse);
    ReuseStrategy::doneWith(serverRequest);

    typename ReuseStrategy::SingleUseString responseString(reusableResponseString);
    serverResponse.SerializePartialToString(&responseString);
    ReuseStrategy::doneWith(serverResponse);

    SINGLE_USE(Response) clientResponse(reusableClientResponse);
    clientResponse.ParsePartialFromString(responseString);

    if (!TestCase::checkResponse(clientResponse, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    ReuseStrategy::doneWith(clientResponse);
  }
}

template <typename TestCase, typename ReuseStrategy, typename Func>
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

    server<TestCase, ReuseStrategy>(clientToServer[0], serverToClient[1], iters);

    int status;
    if (waitpid(child, &status, 0) != child) {
      throw OsException(errno);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      throw std::logic_error("Child exited abnormally.");
    }
  }
}

template <typename ReuseStrategy>
void doBenchmark(const std::string& mode, uint64_t iters) {
  if (mode == "client") {
    syncClient<ExpressionTestCase, ReuseStrategy>(STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "server") {
    server<ExpressionTestCase, ReuseStrategy>(STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "object") {
    passByObject<ExpressionTestCase, ReuseStrategy>(iters);
  } else if (mode == "bytes") {
    passByBytes<ExpressionTestCase, ReuseStrategy>(iters);
  } else if (mode == "pipe") {
    passByPipe<ExpressionTestCase, ReuseStrategy>(
        syncClient<ExpressionTestCase, ReuseStrategy>, iters);
  } else if (mode == "pipe-async") {
    passByPipe<ExpressionTestCase, ReuseStrategy>(
        asyncClient<ExpressionTestCase, ReuseStrategy>, iters);
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::cerr << "USAGE:  " << argv[0] << " MODE REUSE ITERATION_COUNT" << std::endl;
    return 1;
  }

  uint64_t iters = strtoull(argv[3], nullptr, 0);
  srand(123);

  std::cerr << "Doing " << iters << " iterations..." << std::endl;

  std::string reuse = argv[2];
  if (reuse == "reuse") {
    doBenchmark<ReusableMessages>(argv[1], iters);
  } else if (reuse == "no-reuse") {
    doBenchmark<SingleUseMessages>(argv[1], iters);
  } else {
    std::cerr << "Unknown reuse mode: " << reuse << std::endl;
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
