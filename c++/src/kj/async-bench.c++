#include <kj/async.h>
#include <kj/benchmark.h>

static void readyNow(kj::BenchmarkState &state) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = [] () -> kj::Promise<void> {
      return kj::READY_NOW;
    }();
    promise.wait(waitScope);
  }
}

KJ_BENCHMARK(readyNow);

static void coroCoReturn(kj::BenchmarkState &state) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  for (auto _ : state) {
    auto promise = [] () -> kj::Promise<void> {
      co_return;
    }();
    promise.wait(waitScope);
  }
}

KJ_BENCHMARK(coroCoReturn);

KJ_BENCHMARK_MAIN();
