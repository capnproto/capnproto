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

#include "kj/async.h"
#include "kj/common.h"
#include "kj/debug.h"
#include "kj/exception.h"
#include <kj/fut.h>
#include <kj/test.h>

namespace kj {
namespace {

kj::Promise<void> readyNow() { return kj::READY_NOW; }

kj::Promise<void> delayedPromise() {
  return kj::evalLater([]() {});
}

Fut<void> empty() { co_return; }

KJ_TEST("empty") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  empty().wait(waitScope);
}

Fut<size_t> immediate() { co_return 42; }

KJ_TEST("immediate") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  KJ_ASSERT(42 == immediate().wait(waitScope));
}

KJ_TEST("co_await immediate") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<size_t> { co_return co_await immediate(); };
  KJ_ASSERT(42 == fut().wait(waitScope));
}

KJ_TEST("Fut coroutines are eager") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  size_t val = 0;
  // create a future but don't wait on it
  auto fut = [](size_t *ptr) -> Fut<void> {
    *ptr = 42;
    co_return;
  }(&val);
  KJ_EXPECT(val == 42);
}

KJ_TEST("Fut coroutines can be lazy") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  size_t val = 0;

  auto fut = [](size_t *ptr) -> Fut<void, true /* lazy */> {
    *ptr = 42;
    co_return;
  }(&val);
  // - coroutine gets allocated
  // - c = FutCoro<void, true> is initialized in the frame
  // - c.get_return_object is called
  //      - f = Fut<void>(c) is created and returned
  // - c.initial_suspend is called, we return suspend_always
  // - coroutine gets suspended

  KJ_EXPECT(val == 0);

  fut.wait(waitScope);
  KJ_EXPECT(val == 42);
}

KJ_TEST("co_await discard result") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await immediate(); };
  fut().wait(waitScope);
}

KJ_TEST("co_await empty") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await empty(); };
  fut().wait(waitScope);
}

KJ_TEST("co_await readyNow") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await readyNow(); };

  // - coro frame gets allocated
  //  - c = FutCoro<void> initialized in the frame
  //    - c.get_return_object is called
  //      - f = Fut<void>(handle_from_promise(p)) is created and returned
  //    - c.initial_suspend is called, we return suspend_never
  //    - c.await_transform is called on readyNow()
  //    - a = PromiseFutAwaiter is initialized
  //     - a.await_ready is called, we return false since promises always
  //     suspend first
  //     - a.await_suspend(handle(p)) is called to suspend the coro frame, it
  //     returns true (suspend)
  //     - kj event loop calls a.fire()
  //     - a.fire() resumes handle(p)
  //     - a.await_resume is called
  //     - a is destroyed
  //    - c.return_void() is called
  //       - f.resolve() is called
  //    - c is destroyed
  // - coro frame is destroyed

  fut().wait(waitScope);
}

KJ_TEST("co_await delayed promise") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await delayedPromise(); };
  fut().wait(waitScope);
}

KJ_TEST("eager throw exception") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  size_t val = 0;
  auto fut = [](size_t *ptr) -> Fut<void> {
    *ptr = 42;
    kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "request canceled"));
    co_return;
  }(&val);

  // fut has already thrown an exception, which is not raised to us, but
  // recorded in the fut.result.
  KJ_ASSERT(val == 42);
  try {
    fut.wait(waitScope);
    KJ_UNREACHABLE;
  } catch (...) {
    auto e = kj::getCaughtExceptionAsKj();
    KJ_ASSERT(e.getDescription() == "request canceled");
    KJ_ASSERT(e.getType() == kj::Exception::Type::DISCONNECTED);
  }
}

LazyFut<void> lazyEmpty() { co_return; }

KJ_TEST("co_await lazy fut") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await lazyEmpty(); }();

  // - coro frame1 allocated
  // - c1 = FutCoro<void, false> initialized in the frame1
  // - c1.get_return_object is called
  //   - f1 = Fut<void>(frame1) is created and returned
  // - c1.initial_suspend is called, we return suspend_never
  // - coro frame2 allocated
  // - c2 = FutCoro<void, true /* LAZY */> initialized in the frame2
  // - c2.get_return_object is called
  //   - f2 = Fut<void>(frame2) is created and returned
  // - c2.initial_suspend is called, we return LazyInitialSuspend
  //   - LazyInitialSuspend::await_ready is called, we return false
  //   - LazyInitialSuspend::await_suspend(frame2) is called
  // - c1.await_transform(f2) is called
  // - f2.await_ready() is called which returns false
  // - f2.await_suspend(frame1) is called

  KJ_DBG("===>>>>>>>>>> wait");
  fut.wait(waitScope);

  // - f1.wait()
  // - n1 = FutCoroNode(f1) is created
  // - n1.onReady() is called by the event loop
  // - LazyInitialSuspend evalLater callback resumes frame2
  // - LazyInitialSuspend.await_resume is called
  // - LazyInitialSuspend is destroyed
  // - f2.return_void() is called which result in  f2.resolveValue()
  // - f2.final_suspend() returns suspend_never
  // - c2 is destroyed
  // - frame2 is freed

  // error

  KJ_DBG("<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>> wait");
}

LazyFut<void> lazyThrowException(size_t *ptr) {
  *ptr = 42;
  kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "request canceled"));
  co_return;
}

KJ_TEST("co_await lazy throw exception") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  size_t val = 0;
  auto fut = [](size_t *ptr) -> Fut<void> {
    co_await lazyThrowException(ptr);
  }(&val);

  // lazyThrowException is LazyFut, so it didn't start executing yet.
  KJ_ASSERT(val == 0);
  try {
    fut.wait(waitScope);
    KJ_UNREACHABLE;
  } catch (...) {
    auto e = kj::getCaughtExceptionAsKj();
    KJ_ASSERT(e.getDescription() == "request canceled");
    KJ_ASSERT(e.getType() == kj::Exception::Type::DISCONNECTED);
  }
  KJ_ASSERT(val == 42);
}

KJ_TEST("exclusiveJoin") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> {
    co_await exclusiveJoin(empty(), Fut<void, false>::never());
  }();

  fut.wait(waitScope);
}

kj::Fut<size_t> fact(size_t i) {
  if (i == 1)
    co_return 1;
  co_return i *co_await fact(i - 1);
}

KJ_TEST("factorial") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  KJ_ASSERT(2432902008176640000 == fact(20).wait(waitScope));
}

kj::Fut<size_t> fact(size_t i, fut::Stack &stack) {
  if (i == 1)
    co_return 1;
  co_return i *co_await fact(i - 1, stack);
}

KJ_TEST("factorial with stack") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  fut::Stack stack;
  KJ_ASSERT(2432902008176640000 == fact(20, stack).wait(waitScope));
}


KJ_TEST("queue") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  kj::fut::Queue queue;

  auto coro = [&]() -> kj::Fut<void> {
    auto spot = co_await queue.join();
  }();
  coro.wait(waitScope);
}

/*


KJ_TEST("Queue - basic queueUp and finish") {
  KJ_EXPECT(queue.isEmpty());
  KJ_EXPECT(!queue.isActive());

  queue.queueUp();
  KJ_EXPECT(!queue.isEmpty());
  KJ_EXPECT(queue.isActive());

  queue.finish();
  KJ_EXPECT(queue.isEmpty());
  KJ_EXPECT(!queue.isActive());
}

KJ_TEST("Queue - multiple queueUp and finish") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  queue.queueUp();
  queue.queueUp();
  queue.queueUp();

  KJ_EXPECT(!queue.isEmpty());
  KJ_EXPECT(queue.isActive());

  queue.finish();
  KJ_EXPECT(!queue.isEmpty());
  KJ_EXPECT(!queue.isActive());

  queue.queueUp();
  KJ_EXPECT(queue.isActive());

  queue.finish();
  queue.finish();

  KJ_EXPECT(queue.isEmpty());
  KJ_EXPECT(!queue.isActive());
}

KJ_TEST("Queue - queueAndWait basic") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  bool done = false;
  auto promise = queue.queueAndWait().then([&]() { done = true; });

  KJ_EXPECT(!done);
  KJ_EXPECT(!queue.isEmpty());
  KJ_EXPECT(queue.isActive());

  promise.wait(waitScope);

  KJ_EXPECT(done);
  KJ_EXPECT(queue.isActive());

  queue.finish();
  KJ_EXPECT(queue.isEmpty());
}

KJ_TEST("Queue - queueAndWait serialization") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  int counter = 0;

  auto promise1 = queue.queueAndWait().then([&]() {
    KJ_EXPECT(counter == 0);
    counter++;
  });

  auto promise2 = queue.queueAndWait().then([&]() {
    KJ_EXPECT(counter == 1);
    counter++;
  });

  auto promise3 = queue.queueAndWait().then([&]() {
    KJ_EXPECT(counter == 2);
    counter++;
  });

  KJ_EXPECT(counter == 0);

  // First promise should resolve immediately
  promise1.wait(waitScope);
  KJ_EXPECT(counter == 1);
  KJ_EXPECT(queue.isActive());

  queue.finish();

  // Second promise should now be ready
  promise2.wait(waitScope);
  KJ_EXPECT(counter == 2);

  queue.finish();

  // Third promise should now be ready
  promise3.wait(waitScope);
  KJ_EXPECT(counter == 3);

  queue.finish();
  KJ_EXPECT(queue.isEmpty());
}

KJ_TEST("Queue - abort") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  queue.queueUp();
  KJ_EXPECT(queue.isActive());

  queue.abort();
  KJ_EXPECT(!queue.isActive());
  KJ_EXPECT(queue.isEmpty());
}

KJ_TEST("Queue - abort with queueAndWait") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  auto promise = queue.queueAndWait();
  promise.wait(waitScope);

  KJ_EXPECT(queue.isActive());

  queue.abort();
  KJ_EXPECT(!queue.isActive());
  KJ_EXPECT(queue.isEmpty());
}

KJ_TEST("Queue - join basic") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  auto joinPromise = queue.join();

  // join should resolve immediately when queue is empty
  joinPromise.wait(waitScope);
}

KJ_TEST("Queue - join waits for pending messages") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  auto promise1 = queue.queueAndWait();
  auto joinPromise = queue.join();

  // join should not be ready yet
  KJ_EXPECT(!joinPromise.poll(waitScope));

  promise1.wait(waitScope);
  queue.finish();

  // Now join should be ready
  joinPromise.wait(waitScope);
}

KJ_TEST("Queue - join with multiple pending") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  auto promise1 = queue.queueAndWait();
  auto promise2 = queue.queueAndWait();
  auto joinPromise = queue.join();

  KJ_EXPECT(!joinPromise.poll(waitScope));

  promise1.wait(waitScope);
  queue.finish();

  KJ_EXPECT(!joinPromise.poll(waitScope));

  promise2.wait(waitScope);
  queue.finish();

  // Now join should be ready
  joinPromise.wait(waitScope);
}

KJ_TEST("Queue - mixed queueUp and queueAndWait") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  queue.queueUp();
  KJ_EXPECT(!queue.isEmpty());

  auto promise = queue.queueAndWait();
  KJ_EXPECT(!promise.poll(waitScope));

  queue.finish();

  promise.wait(waitScope);
  KJ_EXPECT(queue.isActive());

  queue.finish();
  KJ_EXPECT(queue.isEmpty());
}

KJ_TEST("Queue - isEmpty tracking") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  KJ_EXPECT(queue.isEmpty());

  queue.queueUp();
  KJ_EXPECT(!queue.isEmpty());

  queue.queueUp();
  KJ_EXPECT(!queue.isEmpty());

  queue.finish();
  KJ_EXPECT(!queue.isEmpty());

  queue.finish();
  KJ_EXPECT(queue.isEmpty());
}

KJ_TEST("Queue - isActive tracking") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  KJ_EXPECT(!queue.isActive());

  queue.queueUp();
  KJ_EXPECT(queue.isActive());

  queue.finish();
  KJ_EXPECT(!queue.isActive());

  auto promise = queue.queueAndWait();
  promise.wait(waitScope);
  KJ_EXPECT(queue.isActive());

  queue.finish();
  KJ_EXPECT(!queue.isActive());
}

KJ_TEST("Queue - coroutine integration") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  auto coro = [&]() -> kj::Promise<void> {
    co_await queue.queueAndWait();
    KJ_EXPECT(queue.isActive());
    queue.finish();

    co_await queue.queueAndWait();
    KJ_EXPECT(queue.isActive());
    queue.finish();

    KJ_EXPECT(queue.isEmpty());
  };

  coro().wait(waitScope);
}

KJ_TEST("Queue - join doesn't interfere with operations") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  Queue queue;

  auto promise1 = queue.queueAndWait();
  auto joinPromise1 = queue.join();
  auto joinPromise2 = queue.join();

  promise1.wait(waitScope);
  queue.finish();

  joinPromise1.wait(waitScope);
  joinPromise2.wait(waitScope);

  // Should still be able to use queue after join
  auto promise2 = queue.queueAndWait();
  promise2.wait(waitScope);
  queue.finish();

  KJ_EXPECT(queue.isEmpty());
}
*/

} // namespace
} // namespace kj