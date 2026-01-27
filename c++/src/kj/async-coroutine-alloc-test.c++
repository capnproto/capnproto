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

#include <kj/array.h>
#include <kj/async-coroutine-alloc.h>
#include <kj/async.h>
#include <kj/compat/http.h>
#include <kj/debug.h>
#include <kj/exception.h>
#include <kj/test.h>

namespace kj {
namespace {

using _::DefaultCoroutineAllocator;

// CoroutineAllocator::hasAllocator tests
static_assert(!_::CoroutineAllocator::hasAllocator<>);
static_assert(!_::CoroutineAllocator::hasAllocator<int>);
static_assert(!_::CoroutineAllocator::hasAllocator<int, double, char *>);
static_assert(_::CoroutineAllocator::hasAllocator<_::CoroutineAllocator &>);
static_assert(_::CoroutineAllocator::hasAllocator<DebugCoroutineAllocator &>);
static_assert(_::CoroutineAllocator::hasAllocator<int, DebugCoroutineAllocator &>);
static_assert(_::CoroutineAllocator::hasAllocator<DebugCoroutineAllocator &, int>);
static_assert(_::CoroutineAllocator::hasAllocator<
              int, double, DefaultCoroutineAllocator &, char *>);

// CoroutineAllocator::AllocatorType tests
static_assert(isSameType<_::CoroutineAllocator::AllocatorType<>,
                         _::DefaultCoroutineAllocator>());
static_assert(isSameType<_::CoroutineAllocator::AllocatorType<int>,
                         _::DefaultCoroutineAllocator>());
static_assert(isSameType<_::CoroutineAllocator::AllocatorType<int, double>,
                         _::DefaultCoroutineAllocator>());
static_assert(
    isSameType<_::CoroutineAllocator::AllocatorType<DebugCoroutineAllocator &>,
               DebugCoroutineAllocator>());
static_assert(isSameType<
              _::CoroutineAllocator::AllocatorType<DefaultCoroutineAllocator &>,
              DefaultCoroutineAllocator>());
static_assert(
    isSameType<_::CoroutineAllocator::AllocatorType<int, DebugCoroutineAllocator &>,
               DebugCoroutineAllocator>());
static_assert(
    isSameType<_::CoroutineAllocator::AllocatorType<DebugCoroutineAllocator &, int>,
               DebugCoroutineAllocator>());
static_assert(isSameType<_::CoroutineAllocator::AllocatorType<
                             int, double, DefaultCoroutineAllocator &, char *>,
                         DefaultCoroutineAllocator>());
static_assert(isSameType<_::CoroutineAllocator::AllocatorType<
                             DebugCoroutineAllocator &, DefaultCoroutineAllocator &>,
                         DebugCoroutineAllocator>());
static_assert(isSameType<_::CoroutineAllocator::AllocatorType<
                             DefaultCoroutineAllocator &, DebugCoroutineAllocator &>,
                         DefaultCoroutineAllocator>());

KJ_TEST("CoroutineAllocator::getAllocator") {
  DefaultCoroutineAllocator def;
  DebugCoroutineAllocator debug;
  int x = 0;
  double y = 0.0;

  KJ_EXPECT(&_::CoroutineAllocator::getAllocator(debug) == &debug);
  KJ_EXPECT(&_::CoroutineAllocator::getAllocator(def) == &def);
  KJ_EXPECT(&_::CoroutineAllocator::getAllocator(x, debug) == &debug);
  KJ_EXPECT(&_::CoroutineAllocator::getAllocator(debug, x) == &debug);
  KJ_EXPECT(&_::CoroutineAllocator::getAllocator(x, y, def) == &def);
  KJ_EXPECT(&_::CoroutineAllocator::getAllocator(debug, def) == &debug);
  KJ_EXPECT(&_::CoroutineAllocator::getAllocator(def, debug) == &def);
}

template <typename Allocator>
kj::Promise<size_t> immediateCoroutine(Allocator &) {
  co_return 42;
}

KJ_TEST("DefaultCoroutineAllocator") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  _::DefaultCoroutineAllocator allocator;
  auto promise = immediateCoroutine(allocator);
  KJ_EXPECT(promise.wait(waitScope) == 42);
}

KJ_TEST("DebugAllocator") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  DebugCoroutineAllocator allocator;
  auto promise = immediateCoroutine(allocator);
  KJ_EXPECT(promise.wait(waitScope) == 42);
  
  KJ_EXPECT(allocator.totalAllocCount == 1);
  KJ_EXPECT(allocator.totalAllocSize > 0);
  KJ_EXPECT(allocator.totalFreeCount == 1);
  KJ_EXPECT(allocator.totalFreeSize == allocator.totalFreeSize);

}

template <typename Allocator>
kj::Promise<size_t> coroFib(Allocator& alloc, size_t i) {
  if (i <= 10)
    co_return 1;
  co_return (co_await coroFib(alloc, i - 1)) + (co_await coroFib(alloc, i - 2));
}

template <typename Allocator>
kj::Promise<size_t> coroFib10(Allocator& alloc, size_t i) {
  if (i <= 10)
    co_return 1;
  co_return (co_await coroFib10(alloc, i - 1)) + (co_await coroFib10(alloc, i - 2)) +
      (co_await coroFib10(alloc, i - 3)) + (co_await coroFib10(alloc, i - 4)) +
      (co_await coroFib10(alloc, i - 5)) + (co_await coroFib10(alloc, i - 6)) +
      (co_await coroFib10(alloc, i - 7)) + (co_await coroFib10(alloc, i - 8)) +
      (co_await coroFib10(alloc, i - 9)) + (co_await coroFib10(alloc, i - 10));
}

KJ_TEST("Coroutine Frame sizes") {
#if defined(__clang__) && __clang_major__ >= 20 && defined(NDEBUG)
  // Coroutine size varies between compilers and optimization level. We still want to keep track
  // of coroutine sizes. Thus restrict check to newest clang opt build.
  // We intentionally keep the upper bound open to detect when production compiler deviates.
  #define KJ_EXPECT_CORO_SIZE(...) KJ_EXPECT(__VA_ARGS__)
#else
  #define KJ_EXPECT_CORO_SIZE(...) 
#endif

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  {
    DebugCoroutineAllocator allocator;
    auto promise = immediateCoroutine(allocator);
    KJ_EXPECT(allocator.totalAllocCount == 1);
    KJ_EXPECT_CORO_SIZE(allocator.totalAllocSize == 184);
  }

  {
    DebugCoroutineAllocator allocator;
    auto promise = coroFib(allocator, 10);
    KJ_EXPECT(allocator.totalAllocCount == 1);
    KJ_EXPECT_CORO_SIZE(allocator.totalAllocSize == 336);
  }

  {
    DebugCoroutineAllocator allocator;
    auto promise = coroFib10(allocator, 10);
    KJ_EXPECT(allocator.totalAllocCount == 1);
    KJ_EXPECT_CORO_SIZE(allocator.totalAllocSize == 920);
  }
}

} // namespace
} // namespace kj