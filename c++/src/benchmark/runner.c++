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

namespace capnp {
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
  uint64_t objectSize;
  uint64_t messageSize;
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
  result.objectSize = mode == Mode::OBJECT_SIZE ? throughput : 0;
  result.messageSize = mode == Mode::OBJECT_SIZE ? 0 : throughput;
  result.time.real = asNanosecs(end) - asNanosecs(start);
  result.time.user = asNanosecs(usage.ru_utime);
  result.time.sys = asNanosecs(usage.ru_stime);

  return result;
}

void reportTableHeader() {
  cout << setw(40) << left << "Test"
       << setw(10) << right << "obj size"
       << setw(10) << right << "I/O bytes"
       << setw(10) << right << "wall ns"
       << setw(10) << right << "user ns"
       << setw(10) << right << "sys ns"
       << endl;
  cout << setfill('=') << setw(90) << "" << setfill(' ') << endl;
}

void reportResults(const char* name, uint64_t iters, TestResult results) {
  cout << setw(40) << left << name
       << setw(10) << right << (results.objectSize / iters)
       << setw(10) << right << (results.messageSize / iters)
       << setw(10) << right << (results.time.real / iters)
       << setw(10) << right << (results.time.user / iters)
       << setw(10) << right << (results.time.sys / iters)
       << endl;
}

void reportComparisonHeader() {
  cout << setw(40) << left << "Measure"
       << setw(15) << right << "Protobuf"
       << setw(15) << right << "Cap'n Proto"
       << setw(15) << right << "Improvement"
       << endl;
  cout << setfill('=') << setw(85) << "" << setfill(' ') << endl;
}

void reportOldNewComparisonHeader() {
  cout << setw(40) << left << "Measure"
       << setw(15) << right << "Old"
       << setw(15) << right << "New"
       << setw(15) << right << "Improvement"
       << endl;
  cout << setfill('=') << setw(85) << "" << setfill(' ') << endl;
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
  cout << setw(40) << left << name
       << setw(14) << right << Gain(base, protobuf)
       << setw(14) << right << Gain(base, capnproto);

  // Since smaller is better, the "improvement" is the "gain" from capnproto to protobuf.
  cout << setw(14) << right << Gain(capnproto - base, protobuf - base) << endl;
}

void reportComparison(const char* name, const char* unit, double protobuf, double capnproto,
                      uint64_t iters) {
  cout << setw(40) << left << name
       << setw(15-strlen(unit)) << fixed << right << setprecision(2) << (protobuf / iters) << unit
       << setw(15-strlen(unit)) << fixed << right << setprecision(2) << (capnproto / iters) << unit;

  // Since smaller is better, the "improvement" is the "gain" from capnproto to protobuf.
  cout << setw(14) << right << Gain(capnproto, protobuf) << endl;
}

void reportIntComparison(const char* name, const char* unit, uint64_t protobuf, uint64_t capnproto,
                         uint64_t iters) {
  cout << setw(40) << left << name
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
  char origDir[1024];
  if (getcwd(origDir, sizeof(origDir)) == nullptr) {
    perror("getcwd");
    return 1;
  }
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
  Compression compression = Compression::NONE;
  uint64_t iters = 1;
  const char* oldDir = nullptr;

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
    } else if (arg == "snappy") {
      compression = Compression::SNAPPY;
    } else if (arg == "-c") {
      ++i;
      if (i == argc) {
        fprintf(stderr, "-c requires argument.\n");
        return 1;
      }
      oldDir = argv[i];
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return 1;
    }
  }

  // Scale iterations to something reasonable for each case.
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
      cout << "* in-memory I/O" << endl;
      cout << "  * with client and server in the same thread" << endl;
      break;
    case Mode::PIPE_SYNC:
      cout << "* pipe I/O" << endl;
      cout << "  * with client and server in separate processes" << endl;
      cout << "  * client waits for each response before sending next request" << endl;
      break;
    case Mode::PIPE_ASYNC:
      cout << "* pipe I/O" << endl;
      cout << "  * with client and server in separate processes" << endl;
      cout << "  * client sends as many simultaneous requests as it can" << endl;
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

  cout << endl;

  reportTableHeader();

  TestResult nullCase = runTest(
      Product::NULLCASE, testCase, Mode::OBJECT_SIZE, Reuse::YES, compression, iters);
  reportResults("Theoretical best pass-by-object", iters, nullCase);

  TestResult protobufBase = runTest(
      Product::PROTOBUF, testCase, Mode::OBJECTS, Reuse::YES, compression, iters);
  protobufBase.objectSize = runTest(
      Product::PROTOBUF, testCase, Mode::OBJECT_SIZE, Reuse::YES, compression, iters).objectSize;
  reportResults("Protobuf pass-by-object", iters, protobufBase);

  TestResult capnpBase = runTest(
      Product::CAPNPROTO, testCase, Mode::OBJECTS, Reuse::YES, compression, iters);
  capnpBase.objectSize = runTest(
      Product::CAPNPROTO, testCase, Mode::OBJECT_SIZE, Reuse::YES, compression, iters).objectSize;
  reportResults("Cap'n Proto pass-by-object", iters, capnpBase);

  TestResult nullCaseNoReuse = runTest(
      Product::NULLCASE, testCase, Mode::OBJECT_SIZE, Reuse::NO, compression, iters);
  reportResults("Theoretical best w/o object reuse", iters, nullCaseNoReuse);

  TestResult protobufNoReuse = runTest(
      Product::PROTOBUF, testCase, Mode::OBJECTS, Reuse::NO, compression, iters);
  protobufNoReuse.objectSize = runTest(
      Product::PROTOBUF, testCase, Mode::OBJECT_SIZE, Reuse::NO, compression, iters).objectSize;
  reportResults("Protobuf w/o object reuse", iters, protobufNoReuse);

  TestResult capnpNoReuse = runTest(
      Product::CAPNPROTO, testCase, Mode::OBJECTS, Reuse::NO, compression, iters);
  capnpNoReuse.objectSize = runTest(
      Product::CAPNPROTO, testCase, Mode::OBJECT_SIZE, Reuse::NO, compression, iters).objectSize;
  reportResults("Cap'n Proto w/o object reuse", iters, capnpNoReuse);

  TestResult protobuf = runTest(
      Product::PROTOBUF, testCase, mode, Reuse::YES, compression, iters);
  protobuf.objectSize = protobufBase.objectSize;
  reportResults("Protobuf I/O", iters, protobuf);

  TestResult capnp = runTest(
      Product::CAPNPROTO, testCase, mode, Reuse::YES, compression, iters);
  capnp.objectSize = capnpBase.objectSize;
  reportResults("Cap'n Proto I/O", iters, capnp);
  TestResult capnpPacked = runTest(
      Product::CAPNPROTO, testCase, mode, Reuse::YES, Compression::PACKED, iters);
  capnpPacked.objectSize = capnpBase.objectSize;
  reportResults("Cap'n Proto packed I/O", iters, capnpPacked);

  size_t protobufBinarySize = fileSize("protobuf-" + std::string(testCaseName(testCase)));
  size_t capnpBinarySize = fileSize("capnproto-" + std::string(testCaseName(testCase)));
  size_t protobufCodeSize = fileSize(std::string(testCaseName(testCase)) + ".pb.cc")
                          + fileSize(std::string(testCaseName(testCase)) + ".pb.h");
  size_t capnpCodeSize = fileSize(std::string(testCaseName(testCase)) + ".capnp.c++")
                       + fileSize(std::string(testCaseName(testCase)) + ".capnp.h");
  size_t protobufObjSize = fileSize(std::string(testCaseName(testCase)) + ".pb.o");
  size_t capnpObjSize = fileSize(std::string(testCaseName(testCase)) + ".capnp.o");

  TestResult oldNullCase;
  TestResult oldNullCaseNoReuse;
  TestResult oldCapnpBase;
  TestResult oldCapnpNoReuse;
  TestResult oldCapnp;
  TestResult oldCapnpPacked;
  size_t oldCapnpBinarySize = 0;
  size_t oldCapnpCodeSize = 0;
  size_t oldCapnpObjSize = 0;
  if (oldDir != nullptr) {
    if (chdir(origDir) < 0) {
      perror("chdir");
      return 1;
    }
    if (chdir(oldDir) < 0) {
      perror(oldDir);
      return 1;
    }

    oldNullCase = runTest(
        Product::NULLCASE, testCase, Mode::OBJECT_SIZE, Reuse::YES, compression, iters);
    reportResults("Old theoretical best pass-by-object", iters, nullCase);

    oldCapnpBase = runTest(
        Product::CAPNPROTO, testCase, Mode::OBJECTS, Reuse::YES, compression, iters);
    oldCapnpBase.objectSize = runTest(
        Product::CAPNPROTO, testCase, Mode::OBJECT_SIZE, Reuse::YES, compression, iters)
        .objectSize;
    reportResults("Old Cap'n Proto pass-by-object", iters, oldCapnpBase);

    oldNullCaseNoReuse = runTest(
        Product::NULLCASE, testCase, Mode::OBJECT_SIZE, Reuse::NO, compression, iters);
    reportResults("Old theoretical best w/o object reuse", iters, oldNullCaseNoReuse);

    oldCapnpNoReuse = runTest(
        Product::CAPNPROTO, testCase, Mode::OBJECTS, Reuse::NO, compression, iters);
    oldCapnpNoReuse.objectSize = runTest(
        Product::CAPNPROTO, testCase, Mode::OBJECT_SIZE, Reuse::NO, compression, iters).objectSize;
    reportResults("Old Cap'n Proto w/o object reuse", iters, oldCapnpNoReuse);

    oldCapnp = runTest(
        Product::CAPNPROTO, testCase, mode, Reuse::YES, compression, iters);
    oldCapnp.objectSize = oldCapnpBase.objectSize;
    reportResults("Old Cap'n Proto I/O", iters, oldCapnp);
    oldCapnpPacked = runTest(
        Product::CAPNPROTO, testCase, mode, Reuse::YES, Compression::PACKED, iters);
    oldCapnpPacked.objectSize = oldCapnpBase.objectSize;
    reportResults("Old Cap'n Proto packed I/O", iters, oldCapnpPacked);

    oldCapnpBinarySize = fileSize("capnproto-" + std::string(testCaseName(testCase)));
    oldCapnpCodeSize = fileSize(std::string(testCaseName(testCase)) + ".capnp.c++")
                     + fileSize(std::string(testCaseName(testCase)) + ".capnp.h");
    oldCapnpObjSize = fileSize(std::string(testCaseName(testCase)) + ".capnp.o");
  }

  cout << endl;

  reportComparisonHeader();
  reportComparison("memory overhead (vs ideal)",
      nullCase.objectSize, protobufBase.objectSize, capnpBase.objectSize, iters);
  reportComparison("memory overhead w/o object reuse",
      nullCaseNoReuse.objectSize, protobufNoReuse.objectSize, capnpNoReuse.objectSize, iters);
  reportComparison("object manipulation time (us)", "",
      ((int64_t)protobufBase.time.user - (int64_t)nullCase.time.user) / 1000.0,
      ((int64_t)capnpBase.time.user - (int64_t)nullCase.time.user) / 1000.0, iters);
  reportComparison("object manipulation time w/o reuse (us)", "",
      ((int64_t)protobufNoReuse.time.user - (int64_t)nullCaseNoReuse.time.user) / 1000.0,
      ((int64_t)capnpNoReuse.time.user - (int64_t)nullCaseNoReuse.time.user) / 1000.0, iters);
  reportComparison("I/O time (us)", "",
      ((int64_t)protobuf.time.user - (int64_t)protobufBase.time.user) / 1000.0,
      ((int64_t)capnp.time.user - (int64_t)capnpBase.time.user) / 1000.0, iters);
  reportComparison("packed I/O time (us)", "",
      ((int64_t)protobuf.time.user - (int64_t)protobufBase.time.user) / 1000.0,
      ((int64_t)capnpPacked.time.user - (int64_t)capnpBase.time.user) / 1000.0, iters);

  reportIntComparison("message size (bytes)", "", protobuf.messageSize, capnp.messageSize, iters);
  reportIntComparison("packed message size (bytes)", "",
                      protobuf.messageSize, capnpPacked.messageSize, iters);

  reportComparison("binary size (KiB)", "",
      protobufBinarySize / 1024.0, capnpBinarySize / 1024.0, 1);
  reportComparison("generated code size (KiB)", "",
      protobufCodeSize / 1024.0, capnpCodeSize / 1024.0, 1);
  reportComparison("generated obj size (KiB)", "",
      protobufObjSize / 1024.0, capnpObjSize / 1024.0, 1);

  if (oldDir != nullptr) {
    cout << endl;
    reportOldNewComparisonHeader();

    reportComparison("memory overhead",
        oldNullCase.objectSize, oldCapnpBase.objectSize, capnpBase.objectSize, iters);
    reportComparison("memory overhead w/o object reuse",
        oldNullCaseNoReuse.objectSize, oldCapnpNoReuse.objectSize, capnpNoReuse.objectSize, iters);
    reportComparison("object manipulation time (us)", "",
        ((int64_t)oldCapnpBase.time.user - (int64_t)oldNullCase.time.user) / 1000.0,
        ((int64_t)capnpBase.time.user - (int64_t)oldNullCase.time.user) / 1000.0, iters);
    reportComparison("object manipulation time w/o reuse (us)", "",
        ((int64_t)oldCapnpNoReuse.time.user - (int64_t)oldNullCaseNoReuse.time.user) / 1000.0,
        ((int64_t)capnpNoReuse.time.user - (int64_t)oldNullCaseNoReuse.time.user) / 1000.0, iters);
    reportComparison("I/O time (us)", "",
        ((int64_t)oldCapnp.time.user - (int64_t)oldCapnpBase.time.user) / 1000.0,
        ((int64_t)capnp.time.user - (int64_t)capnpBase.time.user) / 1000.0, iters);
    reportComparison("packed I/O time (us)", "",
        ((int64_t)oldCapnpPacked.time.user - (int64_t)oldCapnpBase.time.user) / 1000.0,
        ((int64_t)capnpPacked.time.user - (int64_t)capnpBase.time.user) / 1000.0, iters);

    reportIntComparison("message size (bytes)", "", oldCapnp.messageSize, capnp.messageSize, iters);
    reportIntComparison("packed message size (bytes)", "",
                        oldCapnpPacked.messageSize, capnpPacked.messageSize, iters);

    reportComparison("binary size (KiB)", "",
        oldCapnpBinarySize / 1024.0, capnpBinarySize / 1024.0, 1);
    reportComparison("generated code size (KiB)", "",
        oldCapnpCodeSize / 1024.0, capnpCodeSize / 1024.0, 1);
    reportComparison("generated obj size (KiB)", "",
        oldCapnpObjSize / 1024.0, capnpObjSize / 1024.0, 1);
  }

  return 0;
}

}  // namespace runner
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::runner::main(argc, argv);
}
