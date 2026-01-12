// Copyright (c) 2025 Cloudflare, Inc. and contributors
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

// Micro-benchmarks for KJ async stack.

#include <benchmark/benchmark.h>

#include <kj/async.h>
#include <kj/debug.h>

// kj::READY_NOW is in its own performance class

static void bm_Promise_ReadyNow(benchmark::State &state) {
  // Benchmark waiting for a kj::READY_NOW promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> { return kj::READY_NOW; }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Promise_ReadyNow);

///////////////////////////////
// Benchmarks for immediate promises and coroutines.

kj::Promise<size_t> immediatePromise() { return 42; }

static void bm_Promise_Immediate(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = immediatePromise();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Promise_Immediate);

kj::Promise<size_t> immediateCoroutine() { co_return 42; }

static void bm_Coro_Immediate(benchmark::State &state) {
  // Benchmark waiting for an immediate coroutine.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = immediateCoroutine();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Coro_Immediate);

///////////////////////////////
// Benchmarks for awaiting single immediate promises and coroutines.

static void bm_Promise_ImmediatePromise_Then(benchmark::State &state) {
  // Benchmark coro that co_awaits an immediate coroutine
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = immediatePromise().then([](size_t x) { return; });
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Promise_ImmediatePromise_Then);

static void bm_Coro_CoAwait_ImmediatePromise(benchmark::State &state) {
  // Benchmark coro that co_awaits an immediate coroutine
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> { co_await immediatePromise(); }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Coro_CoAwait_ImmediatePromise);

static void bm_Coro_CoAwait_ImmediateCoroutine(benchmark::State &state) {
  // Benchmark coro that co_awaits an immediate coroutine
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> {
      co_await immediateCoroutine();
    }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Coro_CoAwait_ImmediateCoroutine);

///////////////////////////////
// Pow benchmarks mean to benchmark promise evaluation when the start of the
// chain is immediate value.

// pow2(i) = 2^i by successive doubling of 1.
kj::Promise<size_t> pow2(size_t i) {
  if (i == 0)
    return 1;
  return pow2(i - 1).then([](size_t x) { return x << 1; });
}

static void bm_Promise_Pow2_20(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = pow2(20);
    KJ_REQUIRE(promise.wait(waitScope) == 1ll << 20);
  }
}

BENCHMARK(bm_Promise_Pow2_20);

kj::Promise<size_t> coroPow2(size_t i) {
  if (i == 0)
    co_return 1;
  co_return (co_await coroPow2(i - 1)) << 1;
}

static void bm_Coro_Pow2_20(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = coroPow2(20);
    KJ_REQUIRE(promise.wait(waitScope) == 1ll << 20);
  }
}

BENCHMARK(bm_Coro_Pow2_20);

///////////////////////////////
// shift benchmarks mean to benchmark deep promise chains ending on paf.

// shifts x left by n bits.
kj::Promise<size_t> shift(size_t n, kj::Promise<size_t> x) {
  if (n == 0)
    return x;
  return shift(n - 1, kj::mv(x)).then([](size_t x) { return x << 1; });
}

// benchmarks shift with unresolved paf and its fulfillment
static void bm_Promise_Shift_20(benchmark::State &state) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto paf = kj::newPromiseAndFulfiller<size_t>();
    auto promise = shift(20, kj::mv(paf.promise));
    paf.fulfiller->fulfill(3);
    KJ_REQUIRE(promise.wait(waitScope) == (1ll << 20) * 3);
  }
}

BENCHMARK(bm_Promise_Shift_20);

// shifts x left by n bits.
kj::Promise<size_t> coroShift(size_t n, kj::Promise<size_t> x) {
  if (n == 0)
    co_return co_await x;
  co_return (co_await coroShift(n - 1, kj::mv(x))) << 1;
}

// benchmarks coroutine shift with unresolved paf and its fulfillment
static void bm_Coro_Shift_20(benchmark::State &state) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto paf = kj::newPromiseAndFulfiller<size_t>();
    auto promise = coroShift(20, kj::mv(paf.promise));
    paf.fulfiller->fulfill(3);
    KJ_REQUIRE(promise.wait(waitScope) == (1ll << 20) * 3);
  }
}

BENCHMARK(bm_Coro_Shift_20);

///////////////////////////////
// fib benchmarks mean to benchmark many await points within a single coro
// these benchmark compute variant of fib function that sums previous 10 numbers.

kj::Promise<size_t> promiseFib10(size_t i) {
  if (i <= 10)
    return 1;
  return promiseFib10(i - 1).then([=](size_t x1) {
    return promiseFib10(i - 2).then([=](size_t x2) {
      return promiseFib10(i - 3).then([=](size_t x3) {
        return promiseFib10(i - 4).then([=](size_t x4) {
          return promiseFib10(i - 5).then([=](size_t x5) {
            return promiseFib10(i - 6).then([=](size_t x6) {
              return promiseFib10(i - 7).then([=](size_t x7) {
                return promiseFib10(i - 8).then([=](size_t x8) {
                  return promiseFib10(i - 9).then([=](size_t x9) {
                    return promiseFib10(i - 10).then([=](size_t x10) {
                      return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10;
                    });
                  });
                });
              });
            });
          });
        });
      });
    });
  });
}

static void bm_Promise_Fib10(benchmark::State &state) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = promiseFib10(12);
    KJ_REQUIRE(promise.wait(waitScope) == 19);
  }
}

BENCHMARK(bm_Promise_Fib10);

kj::Promise<size_t> coroFib10(size_t i) {
  if (i <= 10)
    co_return 1;
  co_return (co_await coroFib10(i - 1)) + (co_await coroFib10(i - 2)) +
      (co_await coroFib10(i - 3)) + (co_await coroFib10(i - 4)) +
      (co_await coroFib10(i - 5)) + (co_await coroFib10(i - 6)) +
      (co_await coroFib10(i - 7)) + (co_await coroFib10(i - 8)) +
      (co_await coroFib10(i - 9)) + (co_await coroFib10(i - 10));
}

static void bm_Coro_Fib10(benchmark::State &state) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = coroFib10(12);
    KJ_REQUIRE(promise.wait(waitScope) == 19);
  }
}

BENCHMARK(bm_Coro_Fib10);

BENCHMARK_MAIN();
