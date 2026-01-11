// Copyright (c) 2026 Cloudflare, Inc. and contributors
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

// Micro-benchmarks comparing KJ_TRY/KJ_CATCH vs raw try/catch

#include <benchmark/benchmark.h>
#include <kj/exception.h>
#include <kj/debug.h>

// Happy path benchmarks - no exceptions thrown

static void bm_Exception_KjTryCatch_Optimized_HappyPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY {
      // Simple computation that doesn't throw
      volatile int x = 42;
      (void)x;
    } KJ_CATCH(_) {
      benchmark::DoNotOptimize(_);
      KJ_FAIL_ASSERT("should not be reached");
    }
  }
}

static void bm_Exception_KjTryCatch_Maybe_HappyPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY_MAYBE {
      // Simple computation that doesn't throw
      volatile int x = 42;
      (void)x;
    } KJ_CATCH_MAYBE(_) {
      benchmark::DoNotOptimize(_);
      KJ_FAIL_ASSERT("should not be reached");
    }
  }
}

static void bm_Exception_RawTryCatch_HappyPath(benchmark::State &state) {
  for (auto _ : state) {
    try {
      // Simple computation that doesn't throw
      volatile int x = 42;
      (void)x;
    } catch (...) {
      auto e = kj::getCaughtExceptionAsKj();
      benchmark::DoNotOptimize(e);
      KJ_FAIL_ASSERT("should not be reached");
    }
  }
}

// Sad path benchmarks - exceptions thrown

static void bm_Exception_KjTryCatch_Optimized_SadPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY {
      KJ_FAIL_ASSERT("test exception");
    } KJ_CATCH(e) {
      // Consume the exception to prevent optimization
      benchmark::DoNotOptimize(e.getDescription().cStr());
    }
  }
}

static void bm_Exception_KjTryCatch_Maybe_SadPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY_MAYBE {
      KJ_FAIL_ASSERT("test exception");
    } KJ_CATCH_MAYBE(e) {
      // Consume the exception to prevent optimization
      benchmark::DoNotOptimize(e.getDescription().cStr());
    }
  }
}

static void bm_Exception_RawTryCatch_SadPath(benchmark::State &state) {
  for (auto _ : state) {
    try {
      KJ_FAIL_ASSERT("test exception");
    } catch (...) {
      auto e = kj::getCaughtExceptionAsKj();
      // Consume the exception to prevent optimization
      benchmark::DoNotOptimize(e.getDescription().cStr());
    }
  }
}

// Inlined benchmarks

static void bm_Exception_KjTryCatch_Optimized_Inlined_HappyPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY {
      // Simple computation that doesn't throw
      volatile int x = 42;
      (void)x;
    } KJ_CATCH_INLINED(e) {
      benchmark::DoNotOptimize(e);
      KJ_FAIL_ASSERT("should not be reached");
    }
  }
}

static void bm_Exception_KjTryCatch_Maybe_Inlined_HappyPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY_MAYBE {
      // Simple computation that doesn't throw
      volatile int x = 42;
      (void)x;
    } KJ_CATCH_MAYBE_INLINED(e) {
      benchmark::DoNotOptimize(e);
      KJ_FAIL_ASSERT("should not be reached");
    }
  }
}

static void bm_Exception_KjTryCatch_Optimized_Inlined_SadPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY {
      KJ_FAIL_ASSERT("test exception");
    } KJ_CATCH_INLINED(e) {
      // Consume the exception to prevent optimization
      benchmark::DoNotOptimize(e.getDescription().cStr());
    }
  }
}

static void bm_Exception_KjTryCatch_Maybe_Inlined_SadPath(benchmark::State &state) {
  for (auto _ : state) {
    KJ_TRY_MAYBE {
      KJ_FAIL_ASSERT("test exception");
    } KJ_CATCH_MAYBE_INLINED(e) {
      // Consume the exception to prevent optimization
      benchmark::DoNotOptimize(e.getDescription().cStr());
    }
  }
}

BENCHMARK(bm_Exception_KjTryCatch_Optimized_HappyPath);
BENCHMARK(bm_Exception_KjTryCatch_Maybe_HappyPath);
BENCHMARK(bm_Exception_RawTryCatch_HappyPath);
BENCHMARK(bm_Exception_KjTryCatch_Optimized_Inlined_HappyPath);
BENCHMARK(bm_Exception_KjTryCatch_Maybe_Inlined_HappyPath);

BENCHMARK(bm_Exception_KjTryCatch_Optimized_SadPath);
BENCHMARK(bm_Exception_KjTryCatch_Maybe_SadPath);
BENCHMARK(bm_Exception_RawTryCatch_SadPath);
BENCHMARK(bm_Exception_KjTryCatch_Optimized_Inlined_SadPath);
BENCHMARK(bm_Exception_KjTryCatch_Maybe_Inlined_SadPath);

BENCHMARK_MAIN();