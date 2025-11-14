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

kj::Promise<size_t> immediate() { return 42; }

static void bm_ReadyNow(benchmark::State &state) {
  // Benchmark waiting for a kj::READ_NOW promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> { return kj::READY_NOW; }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_ReadyNow);

static void bm_Immediate(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = immediate();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Immediate);

static void bm_coroCoReturn(benchmark::State &state) {
  // Benchmark waiting for a co_return coroutine.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> { co_return; }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_coroCoReturn);

static void bm_coroCoAwaitImmediate(benchmark::State &state) {
  // Benchmark coro that co_awaits an immediate coroutine
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> { co_await immediate(); }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_coroCoAwaitImmediate);

BENCHMARK_MAIN();
