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

#pragma once

// Simple benchmarking framework compatible with Google Benchmark.
// Falls back to KJ_TEST when Google Benchmark is not available.
//
// Usage:
//
//    static void benchmarkSomething(kj::BenchmarkState &state) {
//        for (auto _ : state) {
//            // Code to benchmark
//        }
//    }
//
//    KJ_BENCHMARK(benchmarkSomething);
//    KJ_BENCHMARK_MAIN();

#if __has_include(<benchmark/benchmark.h>)
// Google Benchmark is available

#include <benchmark/benchmark.h>

#define KJ_BENCHMARK(x) BENCHMARK(x)
#define KJ_BENCHMARK_MAIN() BENCHMARK_MAIN()

namespace kj {
    using BenchmarkState = benchmark::State;
}

#else
// Google Benchmark is not available

#include <kj/test.h>

#define KJ_BENCHMARK(x) KJ_TEST(#x) { kj::BenchmarkState s{kj::TestCase::iterCount()}; x(s); }
#define KJ_BENCHMARK_MAIN()

namespace kj {
    struct BenchmarkState {
        size_t iterCount;

        struct Iterator {
            size_t i;
            bool operator==(const Iterator& other) const { return i == other.i; }
            Iterator& operator++() { ++i; return *this; }
            size_t operator*() const { return i; }
        };

        Iterator begin() { return {0}; }
        Iterator end() { return {iterCount}; }
    };
}

#endif