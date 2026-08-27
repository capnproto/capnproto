// Copyright (c) 2018 Kenton Varda and contributors
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

// Micro-benchmarks for KJ tables.

#include <benchmark/benchmark.h>

#include "hash.h"
#include "table.h"
#include <kj/test.h>
#include <set>
#include <unordered_set>

namespace kj {
namespace _ {
namespace {

#if defined(KJ_DEBUG) && !__OPTIMIZE__
static constexpr uint BIG_PRIME = 6143;
#else
static constexpr uint BIG_PRIME = 101363;
#endif
static constexpr uint STEPS[] = {1, 2, 4, 7, 43, 127};

void stepArguments(benchmark::Benchmark *benchmark) {
  for (auto step : STEPS) {
    benchmark->Arg(step)->MinTime(0.05);
  }
}

class StringHasher {
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool matches(StringPtr a, StringPtr b) const { return a == b; }
  uint hashCode(StringPtr str) const { return kj::hashCode(str); }
};

class UintHasher {
public:
  uint keyForRow(uint i) const { return i; }

  bool matches(uint a, uint b) const { return a == b; }
  uint hashCode(uint i) const { return kj::hashCode(i); }
};

struct StlStringHash {
  inline size_t operator()(StringPtr str) const { return kj::hashCode(str); }
};

class StringCompare {
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool isBefore(StringPtr a, StringPtr b) const { return a < b; }
  bool matches(StringPtr a, StringPtr b) const { return a == b; }
};

class UintCompare {
public:
  uint keyForRow(uint i) const { return i; }

  bool isBefore(uint a, uint b) const { return a < b; }
  bool matches(uint a, uint b) const { return a == b; }
};

static void bm_TableUintHashIndex(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    Table<uint, HashIndex<UintHasher>> table;
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == kj::none);
      KJ_ASSERT(table.find(i * 5 + 124) == kj::none);
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(i * 5 + 123)));
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == kj::none);
      } else {
        uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

BENCHMARK(bm_TableUintHashIndex)->Apply(stepArguments);

static void bm_UnorderedSetUint(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    std::unordered_set<uint> table;
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(i * 5 + 123);
      KJ_ASSERT(iter != table.end());
      uint value = *iter;
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == table.end());
      KJ_ASSERT(table.find(i * 5 + 124) == table.end());
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(i * 5 + 123) > 0);
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == table.end());
      } else {
        auto iter = table.find(i * 5 + 123);
        KJ_ASSERT(iter != table.end());
        uint value = *iter;
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

BENCHMARK(bm_UnorderedSetUint)->Apply(stepArguments);

static void bm_TableStringHashIndex(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i : kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    Table<StringPtr, HashIndex<StringHasher>> table;
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      StringPtr value = KJ_ASSERT_NONNULL(table.find(strings[i]));
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(strings[i])));
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == kj::none);
      } else {
        StringPtr value = KJ_ASSERT_NONNULL(table.find(strings[i]));
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

BENCHMARK(bm_TableStringHashIndex)->Apply(stepArguments);

static void bm_UnorderedSetString(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i : kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    std::unordered_set<StringPtr, StlStringHash> table;
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(strings[i]);
      KJ_ASSERT(iter != table.end());
      StringPtr value = *iter;
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(strings[i]) > 0);
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == table.end());
      } else {
        auto iter = table.find(strings[i]);
        KJ_ASSERT(iter != table.end());
        StringPtr value = *iter;
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

BENCHMARK(bm_UnorderedSetString)->Apply(stepArguments);

static void bm_TableUintTreeIndex(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    Table<uint, TreeIndex<UintCompare>> table;
    table.reserve(SOME_PRIME);
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == kj::none);
      KJ_ASSERT(table.find(i * 5 + 124) == kj::none);
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(i * 5 + 123)));
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == kj::none);
      } else {
        uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

BENCHMARK(bm_TableUintTreeIndex)->Apply(stepArguments);

static void bm_SetUint(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    std::set<uint> table;
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(i * 5 + 123);
      KJ_ASSERT(iter != table.end());
      uint value = *iter;
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == table.end());
      KJ_ASSERT(table.find(i * 5 + 124) == table.end());
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(i * 5 + 123) > 0);
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == table.end());
      } else {
        auto iter = table.find(i * 5 + 123);
        KJ_ASSERT(iter != table.end());
        uint value = *iter;
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

BENCHMARK(bm_SetUint)->Apply(stepArguments);

static void bm_TableStringTreeIndex(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i : kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    Table<StringPtr, TreeIndex<StringCompare>> table;
    table.reserve(SOME_PRIME);
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      StringPtr value = KJ_ASSERT_NONNULL(table.find(strings[i]));
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(strings[i])));
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == kj::none);
      } else {
        auto &value = KJ_ASSERT_NONNULL(table.find(strings[i]));
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

BENCHMARK(bm_TableStringTreeIndex)->Apply(stepArguments);

static void bm_SetString(benchmark::State &state) {
  constexpr uint SOME_PRIME = BIG_PRIME;

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i : kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  auto step = static_cast<uint>(state.range(0));
  KJ_CONTEXT(step);
  for (auto _ : state) {
    std::set<StringPtr> table;
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i : kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(strings[i]);
      KJ_ASSERT(iter != table.end());
      StringPtr value = *iter;
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(strings[i]) > 0);
      }
    }

    for (uint i : kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == table.end());
      } else {
        auto iter = table.find(strings[i]);
        KJ_ASSERT(iter != table.end());
        StringPtr value = *iter;
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

BENCHMARK(bm_SetString)->Apply(stepArguments);

} // namespace
} // namespace _
} // namespace kj

BENCHMARK_MAIN();
