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
#include <kj/fut.h>

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

static void bm_Fut_ReadyNow(benchmark::State &state) {
  // Benchmark waiting for a kj::READY_NOW promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    // this is "cheating" but demonstrates an important point of no virtualization
    auto promise = []() -> auto { return kj::fut::readyNow(); }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_ReadyNow);

static void bm_Fut_CoAwaitReadyNow(benchmark::State &state) {
  // Benchmark waiting for a kj::READY_NOW promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Fut<void> { co_await kj::fut::readyNow(); }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_CoAwaitReadyNow);

// Immediate promises/coroutines

kj::Promise<size_t> immediatePromise() { return 42; }
kj::Fut<size_t> immediateFut() { co_return 42; }


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

kj::Promise<size_t> fact(size_t i) {
  if (i == 1)
    return 1;
  return fact(i - 1).then([i](size_t x) { return i * x; });
}


static void bm_Fut_CoAwaitImmediateFut(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  kj::fut::Stack stack;
  for (auto _ : state) {
    auto fut = []() -> kj::Fut<void> { co_await immediateFut(); }();
    fut.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_CoAwaitImmediateFut);

static void bm_Fut_CoAwaitImmediatePromise(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto fut = []() -> kj::Fut<void> { co_await immediatePromise(); }();
    fut.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_CoAwaitImmediatePromise);


static void bm_Coro_CoAwaitImmediate(benchmark::State &state) {
  // Benchmark coro that co_awaits an immediate coroutine
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> { co_await immediatePromise(); }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Coro_CoAwaitImmediate);


static void bm_Fut_CoReturn(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  kj::fut::Stack stack;
  for (auto _ : state) {
    auto fut = []() -> kj::Fut<void> { co_return; }();
    fut.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_CoReturn);

static void bm_LazyFut_CoReturn(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto fut = []() -> kj::LazyFut<void> { co_return; }();
    fut.wait(waitScope);
  }
}

BENCHMARK(bm_LazyFut_CoReturn);

static void bm_Coro_CoReturn(benchmark::State &state) {
  // Benchmark waiting for a co_return coroutine.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = []() -> kj::Promise<void> { co_return; }();
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Coro_CoReturn);

static void bm_Promise_Fact20(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = fact(20);
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Promise_Fact20);



kj::Fut<size_t> futFact(size_t i) {
  if (i == 1)
    co_return 1;
  co_return i *co_await futFact(i - 1);
}

static void bm_Fut_Fact20_NoStack(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = futFact(20);
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_Fact20_NoStack);

static void bm_Fut_Fact20_ImplicitStack(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  kj::fut::Stack stack;
  for (auto _ : state) {
    auto promise = futFact(20);
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_Fact20_ImplicitStack);

kj::Fut<size_t> futFactExplicitStack(size_t i, kj::fut::Stack& stack) {
  if (i == 1)
    co_return 1;
  co_return i *co_await futFactExplicitStack(i - 1, stack);
}

static void bm_Fut_Fact20_ExplicitStack(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  kj::fut::Stack stack;
  for (auto _ : state) {
    auto promise = futFactExplicitStack(20, stack);
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Fut_Fact20_ExplicitStack);



kj::Promise<size_t> coroFact(size_t i) {
  if (i == 1)
    co_return 1;
  co_return i *co_await coroFact(i - 1);
}

static void bm_Coro_Fact20(benchmark::State &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = coroFact(20);
    promise.wait(waitScope);
  }
}

BENCHMARK(bm_Coro_Fact20);

BENCHMARK_MAIN();
