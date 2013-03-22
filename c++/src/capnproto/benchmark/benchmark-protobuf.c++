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
#include <algorithm>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <snappy/snappy.h>
#include <snappy/snappy-sinksource.h>

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
// Test case:  Cat Rank
//
// The server receives a list of candidate search results with scores.  It promotes the ones that
// mention "cat" in their snippet and demotes the ones that mention "dog", sorts the results by
// descending score, and returns.
//
// The promotion multiplier is large enough that all the results mentioning "cat" but not "dog"
// should end up at the front ofthe list, which is how we verify the result.

static const char* WORDS[] = {
    "foo ", "bar ", "baz ", "qux ", "quux ", "corge ", "grault ", "garply ", "waldo ", "fred ",
    "plugh ", "xyzzy ", "thud "
};
constexpr size_t WORDS_COUNT = sizeof(WORDS) / sizeof(WORDS[0]);

struct ScoredResult {
  double score;
  const SearchResult* result;

  ScoredResult() = default;
  ScoredResult(double score, const SearchResult* result): score(score), result(result) {}

  inline bool operator<(const ScoredResult& other) const { return score > other.score; }
};

class CatRankTestCase {
public:
  typedef SearchResultList Request;
  typedef SearchResultList Response;
  typedef int Expectation;

  static int setupRequest(SearchResultList* request) {
    int count = rand() % 1000;
    int goodCount = 0;

    for (int i = 0; i < count; i++) {
      SearchResult* result = request->add_result();
      result->set_score(1000 - i);
      result->set_url("http://example.com/");
      std::string* url = result->mutable_url();
      int urlSize = rand() % 100;
      for (int j = 0; j < urlSize; j++) {
        url->push_back('a' + rand() % 26);
      }

      bool isCat = rand() % 8 == 0;
      bool isDog = rand() % 8 == 0;
      goodCount += isCat && !isDog;

      std::string* snippet = result->mutable_snippet();
      snippet->push_back(' ');

      int prefix = rand() % 20;
      for (int j = 0; j < prefix; j++) {
        snippet->append(WORDS[rand() % WORDS_COUNT]);
      }

      if (isCat) snippet->append("cat ");
      if (isDog) snippet->append("dog ");

      int suffix = rand() % 20;
      for (int j = 0; j < suffix; j++) {
        snippet->append(WORDS[rand() % WORDS_COUNT]);
      }
    }

    return goodCount;
  }

  static inline void handleRequest(const SearchResultList& request, SearchResultList* response) {
    std::vector<ScoredResult> scoredResults;

    for (auto& result: request.result()) {
      double score = result.score();
      if (result.snippet().find(" cat ") != std::string::npos) {
        score *= 10000;
      }
      if (result.snippet().find(" dog ") != std::string::npos) {
        score /= 10000;
      }
      scoredResults.emplace_back(score, &result);
    }

    std::sort(scoredResults.begin(), scoredResults.end());

    for (auto& result: scoredResults) {
      SearchResult* out = response->add_result();
      out->set_score(result.score);
      out->set_url(result.result->url());
      out->set_snippet(result.result->snippet());
    }
  }

  static inline bool checkResponse(const SearchResultList& response, int expectedGoodCount) {
    int goodCount = 0;
    for (auto& result: response.result()) {
      if (result.score() > 1001) {
        ++goodCount;
      } else {
        break;
      }
    }

    return goodCount == expectedGoodCount;
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

struct Uncompressed {
  typedef google::protobuf::io::FileInputStream InputStream;
  typedef google::protobuf::io::FileOutputStream OutputStream;

  static uint64_t write(const google::protobuf::MessageLite& message,
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

    return size;
  }

  static void read(google::protobuf::io::ZeroCopyInputStream* rawInput,
                   google::protobuf::MessageLite* message) {
    google::protobuf::io::CodedInputStream input(rawInput);
    uint32_t size;
    GOOGLE_CHECK(input.ReadVarint32(&size));

    auto limit = input.PushLimit(size);

    GOOGLE_CHECK(message->MergePartialFromCodedStream(&input) &&
                 input.ConsumedEntireMessage());

    input.PopLimit(limit);
  }

  static void flush(google::protobuf::io::FileOutputStream* output) {
    if (!output->Flush()) throw OsException(output->GetErrno());
  }
};

// =======================================================================================
// The Snappy interface is really obnoxious.  I gave up here and am just reading/writing flat
// arrays in some static scratch space.  This probably gives protobufs an edge that it doesn't
// deserve.

void writeAll(int fd, const void* buffer, size_t size) {
  const char* pos = reinterpret_cast<const char*>(buffer);
  while (size > 0) {
    ssize_t n = write(fd, pos, size);
    GOOGLE_CHECK_GT(n, 0);
    pos += n;
    size -= n;
  }
}

void readAll(int fd, void* buffer, size_t size) {
  char* pos = reinterpret_cast<char*>(buffer);
  while (size > 0) {
    ssize_t n = read(fd, pos, size);
    GOOGLE_CHECK_GT(n, 0);
    pos += n;
    size -= n;
  }
}

static char scratch[1 << 20];
static char scratch2[1 << 20];

struct SnappyCompressed {
  typedef int InputStream;
  typedef int OutputStream;

  static uint64_t write(const google::protobuf::MessageLite& message, int* output) {
    size_t size = message.ByteSize();
    GOOGLE_CHECK_LE(size, sizeof(scratch));

    message.SerializeWithCachedSizesToArray(reinterpret_cast<uint8_t*>(scratch));

    size_t compressedSize = 0;
    snappy::RawCompress(scratch, size, scratch2 + sizeof(uint32_t), &compressedSize);
    uint32_t tag = compressedSize;
    memcpy(scratch2, &tag, sizeof(tag));

    writeAll(*output, scratch2, compressedSize + sizeof(tag));
    return compressedSize + sizeof(tag);
  }

  static void read(int* input, google::protobuf::MessageLite* message) {
    uint32_t size;
    readAll(*input, &size, sizeof(size));
    readAll(*input, scratch, size);

    size_t uncompressedSize;
    GOOGLE_CHECK(snappy::GetUncompressedLength(scratch, size, &uncompressedSize));
    GOOGLE_CHECK(snappy::RawUncompress(scratch, size, scratch2));

    GOOGLE_CHECK(message->ParsePartialFromArray(scratch2, uncompressedSize));
  }

  static void flush(OutputStream*) {}
};

// =======================================================================================

#define REUSABLE(type) \
  typename ReuseStrategy::template Message<typename TestCase::type>::Reusable
#define SINGLE_USE(type) \
  typename ReuseStrategy::template Message<typename TestCase::type>::SingleUse

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t syncClient(int inputFd, int outputFd, uint64_t iters) {
  uint64_t throughput = 0;

  typename Compression::OutputStream output(outputFd);
  typename Compression::InputStream input(inputFd);

  REUSABLE(Request) reusableRequest;
  REUSABLE(Response) reusableResponse;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) request(reusableRequest);
    typename TestCase::Expectation expected = TestCase::setupRequest(&request);
    throughput += Compression::write(request, &output);
    Compression::flush(&output);
    ReuseStrategy::doneWith(request);

    SINGLE_USE(Response) response(reusableResponse);
    Compression::read(&input, &response);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    ReuseStrategy::doneWith(response);
  }

  return throughput;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t asyncClientSender(int outputFd,
                           ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                           uint64_t iters) {
  uint64_t throughput = 0;

  typename Compression::OutputStream output(outputFd);
  REUSABLE(Request) reusableRequest;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) request(reusableRequest);
    expectations->post(TestCase::setupRequest(&request));
    throughput += Compression::write(request, &output);
    Compression::flush(&output);
    ReuseStrategy::doneWith(request);
  }

  return throughput;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
void asyncClientReceiver(int inputFd,
                         ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
                         uint64_t iters) {
  typename Compression::InputStream input(inputFd);
  REUSABLE(Response) reusableResponse;

  for (; iters > 0; --iters) {
    typename TestCase::Expectation expected = expectations->next();
    SINGLE_USE(Response) response(reusableResponse);
    Compression::read(&input, &response);
    if (!TestCase::checkResponse(response, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    ReuseStrategy::doneWith(response);
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
  uint64_t throughput = 0;

  typename Compression::OutputStream output(outputFd);
  typename Compression::InputStream input(inputFd);

  REUSABLE(Request) reusableRequest;
  REUSABLE(Response) reusableResponse;

  for (; iters > 0; --iters) {
    SINGLE_USE(Request) request(reusableRequest);
    Compression::read(&input, &request);

    SINGLE_USE(Response) response(reusableResponse);
    TestCase::handleRequest(request, &response);
    ReuseStrategy::doneWith(request);

    throughput += Compression::write(response, &output);
    Compression::flush(&output);
    ReuseStrategy::doneWith(response);
  }

  return throughput;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t passByObject(uint64_t iters) {
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

  return 0;
}

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t passByBytes(uint64_t iters) {
  uint64_t throughput = 0;

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
    throughput += requestString.size();
    ReuseStrategy::doneWith(clientRequest);

    SINGLE_USE(Request) serverRequest(reusableServerRequest);
    serverRequest.ParsePartialFromString(requestString);

    SINGLE_USE(Response) serverResponse(reusableServerResponse);
    TestCase::handleRequest(serverRequest, &serverResponse);
    ReuseStrategy::doneWith(serverRequest);

    typename ReuseStrategy::SingleUseString responseString(reusableResponseString);
    serverResponse.SerializePartialToString(&responseString);
    throughput += responseString.size();
    ReuseStrategy::doneWith(serverResponse);

    SINGLE_USE(Response) clientResponse(reusableClientResponse);
    clientResponse.ParsePartialFromString(responseString);

    if (!TestCase::checkResponse(clientResponse, expected)) {
      throw std::logic_error("Incorrect response.");
    }
    ReuseStrategy::doneWith(clientResponse);
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
    writeAll(clientToServer[1], &throughput, sizeof(throughput));

    exit(0);
  } else {
    // Server.
    close(clientToServer[1]);
    close(serverToClient[0]);

    uint64_t throughput =
        server<TestCase, ReuseStrategy, Compression>(clientToServer[0], serverToClient[1], iters);

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

template <typename TestCase, typename ReuseStrategy, typename Compression>
uint64_t doBenchmark(const std::string& mode, uint64_t iters) {
  if (mode == "client") {
    return syncClient<TestCase, ReuseStrategy, Compression>(
        STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "server") {
    return server<TestCase, ReuseStrategy, Compression>(
        STDIN_FILENO, STDOUT_FILENO, iters);
  } else if (mode == "object") {
    return passByObject<TestCase, ReuseStrategy, Compression>(iters);
  } else if (mode == "bytes") {
    return passByBytes<TestCase, ReuseStrategy, Compression>(iters);
  } else if (mode == "pipe") {
    return passByPipe<TestCase, ReuseStrategy, Compression>(
        syncClient<TestCase, ReuseStrategy, Compression>, iters);
  } else if (mode == "pipe-async") {
    return passByPipe<TestCase, ReuseStrategy, Compression>(
        asyncClient<TestCase, ReuseStrategy, Compression>, iters);
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    exit(1);
  }
}

template <typename TestCase, typename Compression>
uint64_t doBenchmark2(const std::string& mode, const std::string& reuse, uint64_t iters) {
  if (reuse == "reuse") {
    return doBenchmark<TestCase, ReusableMessages, Compression>(mode, iters);
  } else if (reuse == "no-reuse") {
    return doBenchmark<TestCase, SingleUseMessages, Compression>(mode, iters);
  } else {
    std::cerr << "Unknown reuse mode: " << reuse << std::endl;
    exit(1);
  }
}

template <typename TestCase>
uint64_t doBenchmark3(const std::string& mode, const std::string& reuse,
                      const std::string& compression, uint64_t iters) {
  if (compression == "none") {
    return doBenchmark2<TestCase, Uncompressed>(mode, reuse, iters);
  } else if (compression == "snappy") {
    return doBenchmark2<TestCase, SnappyCompressed>(mode, reuse, iters);
  } else {
    std::cerr << "Unknown compression mode: " << compression << std::endl;
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
  srand(123);

  std::cerr << "Doing " << iters << " iterations..." << std::endl;

  uint64_t throughput;

  std::string testcase = argv[1];
  if (testcase == "eval") {
    throughput = doBenchmark3<ExpressionTestCase>(argv[2], argv[3], argv[4], iters);
  } else if (testcase == "catrank") {
    throughput = doBenchmark3<CatRankTestCase>(argv[2], argv[3], argv[4], iters);
  } else {
    std::cerr << "Unknown test case: " << testcase << std::endl;
    return 1;
  }

  std::cerr << "Average messages size = " << (throughput / iters) << std::endl;

  return 0;
}

}  // namespace protobuf
}  // namespace benchmark
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::protobuf::main(argc, argv);
}
