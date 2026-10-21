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

// Micro-benchmarks for the fast-path (non-failing) cost of kj/debug.h assertion macros.
//
// None of the checks ever fail. Ideally each one costs a single compare-and-branch. Each check
// sits in a KJ_NOINLINE function that does almost nothing else, so the benchmark measures the
// code the macro adds to the caller.
//
// The checks look like the ones in kj/async.c++ hot paths.

#include <benchmark/benchmark.h>

#include <kj/debug.h>

namespace {

struct Object {
  int* ptr = nullptr;
};

thread_local Object* threadLocalObject = nullptr;
thread_local const void* threadLocalCurrent = nullptr;

int* const SENTINEL = reinterpret_cast<int*>(1);

// =======================================================================================
// KJ_REQUIRE_NONNULL on a thread-local pointer, as in Event::requireEventLoop().

KJ_NOINLINE Object& requireObject() {
  return KJ_REQUIRE_NONNULL(threadLocalObject, "No object is set on this thread.");
}

void bm_RequireNonNull_ThreadLocal(benchmark::State& state) {
  Object object;
  threadLocalObject = &object;
  for (auto _: state) {
    benchmark::DoNotOptimize(&requireObject());
  }
  threadLocalObject = nullptr;
}

BENCHMARK(bm_RequireNonNull_ThreadLocal);

// =======================================================================================
// KJ_REQUIRE comparing a thread-local to `this`, as in Event::permitSelfDestruction().

struct Firing {
  KJ_NOINLINE void permit() {
    KJ_REQUIRE(threadLocalCurrent == this, "Needs to be current.");
  }
};

void bm_Require_ThreadLocalEq(benchmark::State& state) {
  Firing firing;
  threadLocalCurrent = &firing;
  for (auto _: state) {
    firing.permit();
  }
  threadLocalCurrent = nullptr;
}

BENCHMARK(bm_Require_ThreadLocalEq);

// =======================================================================================
// KJ_FAIL_REQUIRE under `if (KJ_UNLIKELY(...))` in a noexcept destructor, as in Event::~Event().

struct Destroyed {
  ~Destroyed() noexcept {
    if (KJ_UNLIKELY(threadLocalCurrent == this)) {
      KJ_FAIL_REQUIRE("Destroyed itself.");
    }
  }
};

KJ_NOINLINE void destroy(Destroyed& d) {
  kj::dtor(d);
}

void bm_FailRequire_Dtor(benchmark::State& state) {
  alignas(Destroyed) char storage[sizeof(Destroyed)];
  auto& d = *reinterpret_cast<Destroyed*>(storage);
  for (auto _: state) {
    kj::ctor(d);
    destroy(d);
  }
}

BENCHMARK(bm_FailRequire_Dtor);

// =======================================================================================
// KJ_ASSERT comparing a member against a sentinel, as in PromiseNode::OnReadyEvent::arm().

struct Armed {
  int* event = nullptr;

  KJ_NOINLINE void arm() {
    KJ_ASSERT(event != SENTINEL, "arm() should only be called once");
    event = SENTINEL;
  }
};

void bm_Assert_MemberNe(benchmark::State& state) {
  Armed armed;
  for (auto _: state) {
    armed.event = nullptr;
    armed.arm();
  }
}

BENCHMARK(bm_Assert_MemberNe);

// =======================================================================================
// KJ_ASSERT with a plain boolean expression (no comparison).

KJ_NOINLINE void assertTruthy(Object& object) {
  KJ_ASSERT(object.ptr, "ptr must be set");
}

void bm_Assert_Truthy(benchmark::State& state) {
  int value = 0;
  Object object { &value };
  for (auto _: state) {
    assertTruthy(object);
  }
}

BENCHMARK(bm_Assert_Truthy);

// =======================================================================================
// KJ_ASSERT on locals inside a loop, with the locals as extra arguments: `KJ_ASSERT(a < b, a, b)`.

constexpr size_t LOCALS_SIZE = 64;

KJ_NOINLINE size_t sumBounded(const size_t* values, size_t count, size_t limit) {
  size_t sum = 0;
  for (size_t i = 0; i < count; i++) {
    size_t value = values[i];
    KJ_ASSERT(value < limit, "value out of range", value, limit);
    sum += value;
  }
  return sum;
}

void bm_Assert_LocalsInLoop(benchmark::State& state) {
  size_t values[LOCALS_SIZE];
  for (size_t i = 0; i < LOCALS_SIZE; i++) values[i] = i;
  size_t count = LOCALS_SIZE;
  size_t limit = LOCALS_SIZE;

  for (auto _: state) {
    benchmark::DoNotOptimize(count);
    benchmark::DoNotOptimize(limit);
    benchmark::DoNotOptimize(sumBounded(values, count, limit));
  }
}

BENCHMARK(bm_Assert_LocalsInLoop);

}  // namespace

BENCHMARK_MAIN();
