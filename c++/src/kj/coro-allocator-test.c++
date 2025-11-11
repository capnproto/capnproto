// Copyright (c) 2020 Cloudflare, Inc. and contributors
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

#include <kj/async.h>
#include <kj/array.h>
#include <kj/debug.h>
#include <kj/test.h>

namespace kj {
namespace {

// Coroutine frame sizes are very platform and compiler dependent, so test them
// only on a single optimized configuration.
#if defined(__linux__) && defined(__clang__) && (__clang_major__ == 20) && !defined(KJ_DEBUG)
#define TEST_CORO_FRAME_SIZES
#endif

KJ_TEST("Ready coroutine") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // initialize statistics
  kj::_::CoroAllocator::instance().resetStatistics();
  
  auto coro = [] () -> kj::Promise<void> {
    kj::Own<kj::_::CoroAllocator::Stats> stats = kj::_::CoroAllocator::instance().resetStatistics();

    KJ_ASSERT(stats->free_count == 0);
    KJ_ASSERT(stats->alloc_count == 1);

#if defined(TEST_CORO_FRAME_SIZES)
    KJ_ASSERT(stats->alloc_size == 208);
#else
    KJ_DBG(stats->alloc_size);
#endif
    co_return;
  };

  coro().wait(waitScope);

  auto stats = kj::_::CoroAllocator::instance().resetStatistics();
  KJ_ASSERT(stats->free_count == 1);
  KJ_ASSERT(stats->alloc_count == 0);
}

}  // namespace
}  // namespace kj