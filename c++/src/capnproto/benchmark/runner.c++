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

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <iomanip>

using namespace std;

namespace capnproto {
namespace benchmark {
namespace runner {

struct Times {
  uint64_t real;
  uint64_t user;
  uint64_t sys;

  uint64_t cpu() { return user + sys; }

  Times operator-(const Times& other) {
    Times result;
    result.real = real - other.real;
    result.user = user - other.user;
    result.sys = sys - other.sys;
    return result;
  }
};

uint64_t asNanosecs(const struct timeval& tv) {
  return (uint64_t)tv.tv_sec * 1000000000 + (uint64_t)tv.tv_usec * 1000;
}

Times currentTimes() {
  Times result;

  struct rusage self, children;
  getrusage(RUSAGE_SELF, &self);
  getrusage(RUSAGE_CHILDREN, &children);

  struct timeval real;
  gettimeofday(&real, nullptr);

  result.real = asNanosecs(real);
  result.user = asNanosecs(self.ru_utime) + asNanosecs(children.ru_utime);
  result.sys = asNanosecs(self.ru_stime) + asNanosecs(children.ru_stime);

  return result;
}

struct TestResult {
  uint64_t throughput;
  Times time;
};

enum class Product {
  CAPNPROTO,
  PROTOBUF,
  NULLCASE
};

enum class TestCase {
  EVAL,
  CATRANK,
  CARSALES
};

const char* testCaseName(TestCase testCase) {
  switch (testCase) {
    case TestCase::EVAL:
      return "eval";
    case TestCase::CATRANK:
      return "catrank";
    case TestCase::CARSALES:
      return "carsales";
  }
  // Can't get here.
  return nullptr;
}

enum class Mode {
  OBJECTS,
  OBJECT_SIZE,
  BYTES,
  PIPE_SYNC,
  PIPE_ASYNC
};

enum class Reuse {
  YES,
  NO
};

enum class Compression {
  NONE,
  PACKED,
  SNAPPY
};

TestResult runTest(Product product, TestCase testCase, Mode mode, Reuse reuse,
                   Compression compression, uint64_t iters) {
  char* argv[6];

  string progName;

  switch (product) {
    case Product::CAPNPROTO:
      progName = "capnproto-";
      break;
    case Product::PROTOBUF:
      progName = "protobuf-";
      break;
    case Product::NULLCASE:
      progName = "null-";
      break;
  }

  progName += testCaseName(testCase);
  argv[0] = strdup(progName.c_str());

  switch (mode) {
    case Mode::OBJECTS:
      argv[1] = strdup("object");
      break;
    case Mode::OBJECT_SIZE:
      argv[1] = strdup("object-size");
      break;
    case Mode::BYTES:
      argv[1] = strdup("bytes");
      break;
    case Mode::PIPE_SYNC:
      argv[1] = strdup("pipe");
      break;
    case Mode::PIPE_ASYNC:
      argv[1] = strdup("pipe-async");
      break;
  }

  switch (reuse) {
    case Reuse::YES:
      argv[2] = strdup("reuse");
      break;
    case Reuse::NO:
      argv[2] = strdup("no-reuse");
      break;
  }

  switch (compression) {
    case Compression::NONE:
      argv[3] = strdup("none");
      break;
    case Compression::PACKED:
      argv[3] = strdup("packed");
      break;
    case Compression::SNAPPY:
      argv[3] = strdup("snappy");
      break;
  }

  char itersStr[64];
  sprintf(itersStr, "%llu", (long long unsigned int)iters);
  argv[4] = itersStr;

  argv[5] = nullptr;

  // Make pipe for child to write throughput.
  int childPipe[2];
  if (pipe(childPipe) < 0) {
    perror("pipe");
    exit(1);
  }

  // Spawn the child process.
  struct timeval start, end;
  gettimeofday(&start, nullptr);
  pid_t child = fork();
  if (child == 0) {
    close(childPipe[0]);
    dup2(childPipe[1], STDOUT_FILENO);
    close(childPipe[1]);
    execv(argv[0], argv);
    exit(1);
  }

  close(childPipe[1]);
  for (int i = 0; i < 4; i++) {
    free(argv[i]);
  }

  // Read throughput number written to child's stdout.
  FILE* input = fdopen(childPipe[0], "r");
  long long unsigned int throughput;
  if (fscanf(input, "%lld", &throughput) != 1) {
    fprintf(stderr, "Child didn't write throughput to stdout.");
  }
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), input) != nullptr) {
    // Loop until EOF.
  }
  fclose(input);

  // Wait for child exit.
  int status;
  struct rusage usage;
  wait4(child, &status, 0, &usage);
  gettimeofday(&end, nullptr);

  // Calculate results.

  TestResult result;
  result.throughput = throughput;
  result.time.real = asNanosecs(end) - asNanosecs(start);
  result.time.user = asNanosecs(usage.ru_utime);
  result.time.sys = asNanosecs(usage.ru_stime);

  return result;
}

void reportTableHeader() {
  cout << setw(50) << right << "obj size or"
       << endl;
  cout << setw(40) << left << "Test"
       << setw(10) << right << "I/O bytes"
       << setw(10) << right << "wall ns"
       << setw(10) << right << "user ns"
       << setw(10) << right << "sys ns"
       << endl;
  cout << setfill('=') << setw(80) << "" << setfill(' ') << endl;
}

void reportResults(const char* name, uint64_t iters, TestResult results) {
  cout << setw(40) << left << name
       << setw(10) << right << (results.throughput / iters)
       << setw(10) << right << (results.time.real / iters)
       << setw(10) << right << (results.time.user / iters)
       << setw(10) << right << (results.time.sys / iters)
       << endl;
}

void reportComparisonHeader() {
  cout << setw(35) << left << "Overhead type"
       << setw(15) << right << "Protobuf"
       << setw(15) << right << "Cap'n Proto"
       << setw(15) << right << "Improvement"
       << endl;
  cout << setfill('=') << setw(80) << "" << setfill(' ') << endl;
}

class Gain {
public:
  Gain(double oldValue, double newValue)
      : amount(newValue / oldValue) {}

  void writeTo(std::ostream& os) {
    if (amount < 2) {
      double percent = (amount - 1) * 100;
      os << (int)(percent + 0.5) << "%";
    } else {
      os << fixed << setprecision(2) << amount << "x";
    }
  }

private:
  double amount;
};

ostream& operator<<(ostream& os, Gain gain) {
  gain.writeTo(os);
  return os;
}

void reportComparison(const char* name, double base, double protobuf, double capnproto,
                      uint64_t iters) {
  cout << setw(35) << left << name
       << setw(14) << right << Gain(base, protobuf)
       << setw(14) << right << Gain(base, capnproto);

  // Since smaller is better, the "improvement" is the "gain" from capnproto to protobuf.
  cout << setw(14) << right << Gain(capnproto - base, protobuf - base) << endl;
}

void reportComparison(const char* name, const char* unit, double protobuf, double capnproto,
                      uint64_t iters) {
  cout << setw(35) << left << name
       << setw(15-strlen(unit)) << fixed << right << setprecision(2) << (protobuf / iters) << unit
       << setw(15-strlen(unit)) << fixed << right << setprecision(2) << (capnproto / iters) << unit;

  // Since smaller is better, the "improvement" is the "gain" from capnproto to protobuf.
  cout << setw(14) << right << Gain(capnproto, protobuf) << endl;
}

void reportIntComparison(const char* name, const char* unit, uint64_t protobuf, uint64_t capnproto,
                         uint64_t iters) {
  cout << setw(35) << left << name
       << setw(15-strlen(unit)) << right << (protobuf / iters) << unit
       << setw(15-strlen(unit)) << right << (capnproto / iters) << unit;

  // Since smaller is better, the "improvement" is the "gain" from capnproto to protobuf.
  cout << setw(14) << right << Gain(capnproto, protobuf) << endl;
}

size_t fileSize(const std::string& name) {
  struct stat stats;
  if (stat(name.c_str(), &stats) < 0) {
    perror(name.c_str());
    exit(1);
  }

  return stats.st_size;
}

int main(int argc, char* argv[]) {
  char* path = argv[0];
  char* slashpos = strrchr(path, '/');
  if (slashpos != nullptr) {
    *slashpos = '\0';
    if (chdir(path) < 0) {
      perror("chdir");
      return 1;
    }
    *slashpos = '/';
  }

  TestCase testCase = TestCase::CATRANK;
  Mode mode = Mode::PIPE_SYNC;
  Reuse reuse = Reuse::YES;
  Compression compression = Compression::NONE;
  uint64_t iters = 1;

  for (int i = 1; i < argc; i++) {
    string arg = argv[i];
    if (isdigit(argv[i][0])) {
      iters = strtoul(argv[i], nullptr, 0);
    } else if (arg == "async") {
      mode = Mode::PIPE_ASYNC;
    } else if (arg == "inmem") {
      mode = Mode::BYTES;
    } else if (arg == "eval") {
      testCase = TestCase::EVAL;
    } else if (arg == "carsales") {
      testCase = TestCase::CARSALES;
    } else if (arg == "no-reuse") {
      reuse = Reuse::NO;
    } else if (arg == "packed") {
      compression = Compression::PACKED;
    } else if (arg == "snappy") {
      compression = Compression::SNAPPY;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return 1;
    }
  }

  switch (testCase) {
    case TestCase::EVAL:
      iters *= 100000;
      break;
    case TestCase::CATRANK:
      iters *= 1000;
      break;
    case TestCase::CARSALES:
      iters *= 20000;
      break;
  }

  cout << "Running " << iters << " iterations of ";
  switch (testCase) {
    case TestCase::EVAL:
      cout << "calculator";
      break;
    case TestCase::CATRANK:
      cout << "CatRank";
      break;
    case TestCase::CARSALES:
      cout << "car sales";
      break;
  }

  cout << " example case with:" << endl;

  switch (mode) {
    case Mode::OBJECTS:
    case Mode::OBJECT_SIZE:
      // Can't happen.
      break;
    case Mode::BYTES:
      cout << "* client and server in the same process (passing bytes in memory)" << endl;
      break;
    case Mode::PIPE_SYNC:
      cout << "* client and server passing messages over pipes" << endl;
      cout << "* client sending one request at a time" << endl;
      break;
    case Mode::PIPE_ASYNC:
      cout << "* client and server passing messages over pipes" << endl;
      cout << "* client saturating pipe with requests without waiting for responses" << endl;
      break;
  }
  switch (reuse) {
    case Reuse::YES:
      cout << "* ideal object reuse" << endl;
      break;
    case Reuse::NO:
      cout << "* no object reuse" << endl;
      break;
  }
  switch (compression) {
    case Compression::NONE:
      cout << "* no compression" << endl;
      break;
    case Compression::PACKED:
      cout << "* de-zero packing for Cap'n Proto" << endl;
      cout << "* standard packing for Protobuf" << endl;
      break;
    case Compression::SNAPPY:
      cout << "* Snappy compression" << endl;
      break;
  }

  reportTableHeader();

  TestResult nullCase = runTest(
      Product::NULLCASE, testCase, Mode::OBJECTS, reuse, compression, iters);
  reportResults("Theoretical best pass-by-object", iters, nullCase);

  TestResult protobufBase = runTest(
      Product::PROTOBUF, testCase, Mode::OBJECTS, reuse, compression, iters);
  protobufBase.throughput = runTest(
      Product::PROTOBUF, testCase, Mode::OBJECT_SIZE, reuse, compression, iters).throughput;
  reportResults("Protobuf pass-by-object", iters, protobufBase);

  TestResult capnpBase = runTest(
      Product::CAPNPROTO, testCase, Mode::OBJECTS, reuse, compression, iters);
  capnpBase.throughput = runTest(
      Product::CAPNPROTO, testCase, Mode::OBJECT_SIZE, reuse, compression, iters).throughput;
  reportResults("Cap'n Proto pass-by-object", iters, capnpBase);

  TestResult protobuf = runTest(
      Product::PROTOBUF, testCase, mode, reuse, compression, iters);
  reportResults("Protobuf pass-by-I/O", iters, protobuf);

  TestResult capnp = runTest(
      Product::CAPNPROTO, testCase, mode, reuse, compression, iters);
  reportResults("Cap'n Proto pass-by-I/O", iters, capnp);

  cout << endl;

  reportComparisonHeader();
  reportComparison("memory",
      nullCase.throughput, protobufBase.throughput, capnpBase.throughput, iters);
  reportComparison("object manipulation",
      nullCase.time.cpu(), protobufBase.time.cpu(), capnpBase.time.cpu(), iters);
  reportComparison("I/O time", "us",
      ((int64_t)protobuf.time.cpu() - (int64_t)protobufBase.time.cpu()) / 1000.0,
      ((int64_t)capnp.time.cpu() - (int64_t)capnpBase.time.cpu()) / 1000.0, iters);

  reportIntComparison("bandwidth", "B", protobuf.throughput, capnp.throughput, iters);

  reportComparison("binary size", "kB",
      fileSize("protobuf-" + std::string(testCaseName(testCase))) / 1024.0,
      fileSize("capnproto-" + std::string(testCaseName(testCase))) / 1024.0, 1);
  reportComparison("generated code size", "kB",
      fileSize(std::string(testCaseName(testCase)) + ".pb.cc") / 1024.0
      + fileSize(std::string(testCaseName(testCase)) + ".pb.h") / 1024.0,
      fileSize(std::string(testCaseName(testCase)) + ".capnp.c++") / 1024.0
      + fileSize(std::string(testCaseName(testCase)) + ".capnp.h") / 1024.0, 1);
  reportComparison("generated obj size", "kB",
      fileSize(std::string(testCaseName(testCase)) + ".pb.o") / 1024.0,
      fileSize(std::string(testCaseName(testCase)) + ".capnp.o") / 1024.0, 1);

  return 0;
}

}  // namespace runner
}  // namespace benchmark
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::runner::main(argc, argv);
}
