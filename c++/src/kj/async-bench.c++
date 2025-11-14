#include <kj/async.h>
#include <kj/benchmark.h>

kj::Promise<size_t> immediate() {
  return 42;
}

static void bm_ReadyNow(kj::BenchmarkState &state) {
  // Benchmark waiting for a kj::READ_NOW promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = [] () -> kj::Promise<void> {
      return kj::READY_NOW;
    }();
    promise.wait(waitScope);
  }
}

KJ_BENCHMARK(bm_ReadyNow);

static void bm_Immediate(kj::BenchmarkState &state) {
  // Benchmark waiting for an immediate promise.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = immediate();
    promise.wait(waitScope);
  }
}

KJ_BENCHMARK(bm_Immediate);

static void bm_coroCoReturn(kj::BenchmarkState &state) {
  // Benchmark waiting for a co_return coroutine.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = [] () -> kj::Promise<void> {
      co_return;
    }();
    promise.wait(waitScope);
  }
}

KJ_BENCHMARK(bm_coroCoReturn);

static void bm_coroCoAwaitImmediate(kj::BenchmarkState &state) {
  // Benchmark coro that co_awaits an immediate coroutine
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = [] () -> kj::Promise<void> {
      co_await immediate();
    }();
    promise.wait(waitScope);
  }
}

KJ_BENCHMARK(bm_coroCoAwaitImmediate);

KJ_BENCHMARK_MAIN();
