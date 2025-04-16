// Copyright (c) 2025 Cloudflare, Inc.
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

#include "promises-unordered.h"
#include "debug.h"
#include "test.h"
#include "function.h"  // For FunctionParam
#include <signal.h>    // For SIGABRT
#include <sstream>     // For std::stringstream
#include <string>      // For std::string

namespace kj {
namespace {

// ======================================================================================
// Basic Functionality and Ready Promises
// ======================================================================================

KJ_TEST("PromisesUnordered with a single ready promise") {
  const int MAGIC_VALUE = 42;
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add a single already-fulfilled promise
  pui.add(Promise<int>(MAGIC_VALUE));

  uint iterationCount = 0;

  // Use range-based for loop
  for (Promise<int> promise : pui) {
    int value = promise.wait(waitScope);
    KJ_ASSERT(value == MAGIC_VALUE);
    iterationCount++;
  }

  // We should have iterated exactly once
  KJ_ASSERT(iterationCount == 1);
}

KJ_TEST("PromisesUnordered processes already-fulfilled promises in fulfillment order") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Create three promises with paired fulfillers
  auto paf1 = kj::newPromiseAndFulfiller<int>();
  auto paf2 = kj::newPromiseAndFulfiller<int>();
  auto paf3 = kj::newPromiseAndFulfiller<int>();

  // Add the promises to the PromisesUnordered
  pui.add(kj::mv(paf1.promise));
  pui.add(kj::mv(paf2.promise));
  pui.add(kj::mv(paf3.promise));

  // Fulfill the promises BEFORE starting the coroutine
  int fulfillCounter = 1;
  paf2.fulfiller->fulfill(fulfillCounter++);
  paf3.fulfiller->fulfill(fulfillCounter++);
  paf1.fulfiller->fulfill(fulfillCounter++);

  // Start a coroutine that will collect results from the PromisesUnordered object
  Promise<void> testPromise = [&]() -> Promise<void> {
    int count = 0;

    // Use range-based for loop to await each promise as it becomes ready
    for (Promise<int> promise : pui) {
      int value = co_await promise;
      count++;

      // Each value should match the order of fulfillment
      KJ_ASSERT(value == count);
    }

    // Verify we processed all three promises
    KJ_ASSERT(count == 3);

    co_return;
  }();

  // Wait for the test to complete
  testPromise.wait(waitScope);
}

// ======================================================================================
// Promise Ordering and Fulfillment
// ======================================================================================

KJ_TEST("PromisesUnordered selects ready promise over never-done promise") {
  const int MAGIC_VALUE = 99;
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add a never-done promise first
  pui.add(kj::Promise<int>(kj::NEVER_DONE));

  // Then add a ready promise
  pui.add(Promise<int>(MAGIC_VALUE));

  // Dereference begin iterator to get the first promise that completes
  Promise<int> promise = *pui.begin();

  // It should be the ready promise, not the never-done one
  int value = promise.wait(waitScope);
  KJ_ASSERT(value == MAGIC_VALUE);
}

KJ_TEST("PromisesUnordered processes unready promises in fulfillment order (reversed)") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Create three promises with paired fulfillers
  auto paf1 = kj::newPromiseAndFulfiller<int>();
  auto paf2 = kj::newPromiseAndFulfiller<int>();
  auto paf3 = kj::newPromiseAndFulfiller<int>();

  // Add the promises to the PromisesUnordered
  pui.add(kj::mv(paf1.promise));
  pui.add(kj::mv(paf2.promise));
  pui.add(kj::mv(paf3.promise));

  // Start a coroutine that will collect results from the PromisesUnordered object
  Promise<void> testPromise = [&]() -> Promise<void> {
    int count = 0;

    // Use range-based for loop to await each promise as it becomes ready
    for (Promise<int> promise : pui) {
      int value = co_await promise;
      count++;

      // Each value should match the order of fulfillment
      KJ_ASSERT(value == count);
    }

    // Verify we processed all three promises
    KJ_ASSERT(count == 3);

    co_return;
  }();

  // Fulfill the promises in reverse order with monotonically increasing values
  int fulfillCounter = 1;
  paf3.fulfiller->fulfill(fulfillCounter++);
  paf2.fulfiller->fulfill(fulfillCounter++);
  paf1.fulfiller->fulfill(fulfillCounter++);

  // Wait for the test to complete
  testPromise.wait(waitScope);
}

KJ_TEST("PromisesUnordered processes unready promises in fulfillment order (forward)") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Create three promises with paired fulfillers
  auto paf1 = kj::newPromiseAndFulfiller<int>();
  auto paf2 = kj::newPromiseAndFulfiller<int>();
  auto paf3 = kj::newPromiseAndFulfiller<int>();

  // Add the promises to the PromisesUnordered
  pui.add(kj::mv(paf1.promise));
  pui.add(kj::mv(paf2.promise));
  pui.add(kj::mv(paf3.promise));

  // Start a coroutine that will collect results from the PromisesUnordered object
  Promise<void> testPromise = [&]() -> Promise<void> {
    int count = 0;

    // Use range-based for loop to await each promise as it becomes ready
    for (Promise<int> promise : pui) {
      int value = co_await promise;
      count++;

      // Each value should match the order of fulfillment
      KJ_ASSERT(value == count);
    }

    // Verify we processed all three promises
    KJ_ASSERT(count == 3);

    co_return;
  }();

  // Fulfill the promises in the same order they were added
  int fulfillCounter = 1;
  paf1.fulfiller->fulfill(fulfillCounter++);
  paf2.fulfiller->fulfill(fulfillCounter++);
  paf3.fulfiller->fulfill(fulfillCounter++);

  // Wait for the test to complete
  testPromise.wait(waitScope);
}

KJ_TEST("PromisesUnordered processes a task added after dereferencing an iterator") {
  const int MAGIC_VALUE = 42;
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add a never-done promise first
  pui.add(kj::Promise<int>(kj::NEVER_DONE));

  // Get a begin iterator and dereference it
  auto beginIterator = pui.begin();
  Promise<int> promise = *beginIterator;

  // Add a ready promise with the magic value AFTER dereferencing the iterator
  pui.add(Promise<int>(MAGIC_VALUE));

  // Await the promise from the iterator
  int value = promise.wait(waitScope);

  // It should be the magic value from the second promise
  KJ_ASSERT(value == MAGIC_VALUE);
}

// ======================================================================================
// Dynamic Task Addition and Iteration
// ======================================================================================

KJ_TEST("PromisesUnordered allows adding tasks during iteration") {
  // This test exercises what happens when new tasks are added during iteration
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add an initial promise
  auto paf1 = kj::newPromiseAndFulfiller<int>();
  pui.add(kj::mv(paf1.promise));

  // Immediately fulfill it with value 1
  paf1.fulfiller->fulfill(1);

  // We'll track the values we see
  kj::Vector<int> seenValues;

  // We'll also create additional promises as we go
  // Pre-create the fulfillers so we can fulfill them after adding to pui
  auto paf2 = kj::newPromiseAndFulfiller<int>();
  auto paf3 = kj::newPromiseAndFulfiller<int>();

  // Iterate over promises, adding new ones during iteration
  for (auto promise : pui) {
    // Get the value
    int value = promise.wait(waitScope);
    seenValues.add(value);

    // Add more promises based on which iteration we're on
    switch (seenValues.size()) {
      case 1:
        // After seeing the first value, add a second promise and fulfill it immediately
        pui.add(kj::mv(paf2.promise));
        paf2.fulfiller->fulfill(2);
        break;

      case 2:
        // After seeing the second value, add a third promise and fulfill it immediately
        pui.add(kj::mv(paf3.promise));
        paf3.fulfiller->fulfill(3);
        break;

      case 3:
        // After seeing the third value, don't add any more promises
        // The loop will end naturally because there are no more ready promises
        break;
    }
  }

  // We should have seen 3 values: 1, 2, and 3
  KJ_ASSERT(seenValues.size() == 3);
  KJ_ASSERT(seenValues[0] == 1);
  KJ_ASSERT(seenValues[1] == 2);
  KJ_ASSERT(seenValues[2] == 3);

  // All promises have been fulfilled and consumed, so pui should be empty
  KJ_ASSERT(pui.isEmpty());
}

KJ_TEST("PromisesUnordered isEmpty() becomes true in a for loop after processing all promises") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // Create a collection with two immediately-fulfilled promises
  PromisesUnordered<int> pui;
  pui.add(kj::Promise<int>(42));
  pui.add(kj::Promise<int>(99));

  // At the start, collection is not empty
  KJ_ASSERT(!pui.isEmpty());

  // Start the for loop
  int iterationCount = 0;
  for (Promise<int> promise : pui) {
    // Get a value from the promise
    int value = promise.wait(waitScope);

    // First iteration
    if (iterationCount == 0) {
      KJ_ASSERT(value == 42);

      // After processing the first promise, the collection should still not be empty
      KJ_ASSERT(!pui.isEmpty());
    }

    // Second iteration
    if (iterationCount == 1) {
      KJ_ASSERT(value == 99);

      // After processing the second promise, the collection should now be empty!
      // This is the key assertion for this test: isEmpty() becomes true during the loop
      KJ_ASSERT(pui.isEmpty());
    }

    iterationCount++;
  }

  // We should have processed exactly two promises
  KJ_ASSERT(iterationCount == 2);

  // The collection should be empty after the loop
  KJ_ASSERT(pui.isEmpty());

  // Adding another promise should make it non-empty again
  pui.add(kj::Promise<int>(123));
  KJ_ASSERT(!pui.isEmpty());
}

KJ_TEST("PromisesUnordered handles task destruction that adds new tasks") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // Create a counter that can be shared between promises using refcounting
  struct Counter { uint count = 0; };
  auto counter = kj::refcountedWrapper<Counter>();

  // We'll create this in a separate scope so it's destroyed
  {
    // Create a PromisesUnordered that we'll add to in a task destructor
    PromisesUnordered<int> pui;

    // Create a promise task that, when destroyed, will add many more tasks
    pui.add(kj::Promise<int>(kj::NEVER_DONE).attach(kj::defer([&pui, counter = kj::addRef(*counter)]() mutable {
      // Add a large number of never-done promises from the destructor
      // This would cause a stack overflow if we used recursive destruction
      const uint taskCount = 100000;

      for (uint i = 0; i < taskCount; i++) {
        pui.add(kj::Promise<int>(kj::NEVER_DONE).attach(kj::defer([counter = kj::addRef(*counter)]() mutable {
          // Increment counter when this task is destroyed
          counter->getWrapped().count++;
        })));
      }
    })));

    // Do NOT wait for the promise - just let pui go out of scope
    // The destructor will destroy the first task, which will add 100,000 more tasks,
    // and then destroy those too. If not implemented properly, this would stack overflow.
  }

  // If we reach here without a stack overflow, the test passed
  // We can verify that all tasks were created and destroyed
  KJ_EXPECT(counter->getWrapped().count == 100000);
}

// ======================================================================================
// Exception Handling
// ======================================================================================

KJ_TEST("PromisesUnordered propagates exceptions correctly") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add a promise that will resolve with an exception
  auto paf = newPromiseAndFulfiller<int>();
  pui.add(kj::mv(paf.promise));

  // Reject the promise with an exception
  paf.fulfiller->reject(KJ_EXCEPTION(FAILED, "Test exception"));

  // Try to get the result from the iterator
  auto beginIterator = pui.begin();
  Promise<int> promise = *beginIterator;

  // The exception should be propagated
  KJ_EXPECT_THROW_MESSAGE("Test exception", promise.wait(waitScope));

  // Add a second promise that completes successfully
  pui.add(Promise<int>(42));

  // Make sure we can still iterate and get more results
  auto secondIterator = pui.begin();
  Promise<int> secondPromise = *secondIterator;

  // This should succeed with the value from the second promise
  int value = secondPromise.wait(waitScope);
  KJ_EXPECT(value == 42);
}

KJ_TEST("PromisesUnordered with mixed successful and failed promises") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add three promises, one that fails and two that succeed
  auto paf1 = newPromiseAndFulfiller<int>();
  auto paf2 = newPromiseAndFulfiller<int>();
  auto paf3 = newPromiseAndFulfiller<int>();

  pui.add(kj::mv(paf1.promise));
  pui.add(kj::mv(paf2.promise));
  pui.add(kj::mv(paf3.promise));

  // Complete the first promise
  paf1.fulfiller->fulfill(123);

  // Reject the second promise
  paf2.fulfiller->reject(KJ_EXCEPTION(FAILED, "Second promise failed"));

  // Get first result - should be from the first promise
  {
    auto it = pui.begin();
    Promise<int> promise = *it;
    int value = promise.wait(waitScope);
    KJ_EXPECT(value == 123);
  }

  // Get second result - should be the exception from the second promise
  {
    auto it = pui.begin();
    Promise<int> promise = *it;
    KJ_EXPECT_THROW_MESSAGE("Second promise failed", promise.wait(waitScope));
  }

  // Complete the third promise
  paf3.fulfiller->fulfill(456);

  // Get third result - should be from the third promise
  {
    auto it = pui.begin();
    Promise<int> promise = *it;
    int value = promise.wait(waitScope);
    KJ_EXPECT(value == 456);
  }

  // Should now be empty
  KJ_EXPECT(pui.isEmpty());
}

KJ_TEST("PromisesUnordered handles task destruction exceptions when awaiting") {
  // This test verifies what happens when a task's destructor throws an exception
  // while awaiting a begin promise

  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add a promise that will throw an exception when its destructor runs
  pui.add(kj::Promise<int>(123).attach(kj::defer([] {
    KJ_FAIL_ASSERT("Test error in task destructor");
  })));

  // Get a promise from begin() and await it
  Promise<int> beginPromise = *pui.begin();

  // When we await the promise, it should return the value
  // But then throw the exception when the task is destroyed
  KJ_EXPECT_THROW_MESSAGE("Test error in task destructor", beginPromise.wait(waitScope));

  // At this point, the task with the throwing destructor should have been destroyed,
  // and the exception should have been propagated, so our test should still be running

  // PromisesUnordered should be empty now
  KJ_ASSERT(pui.isEmpty());
}

KJ_TEST("PromisesUnordered handles task destruction exceptions when destroyed") {
  // This test verifies what happens when a task's destructor throws an exception
  // when the PromisesUnordered is destroyed without awaiting

  // We expect the exception from the task destructor to be caught and re-thrown
  KJ_EXPECT_THROW_MESSAGE("Test error in task destructor", {
    EventLoop loop;
    WaitScope waitScope(loop);

    {
      PromisesUnordered<int> pui;

      // Add a promise that will throw an exception when its destructor runs
      pui.add(kj::Promise<int>(123).attach(kj::defer([] {
        KJ_FAIL_ASSERT("Test error in task destructor");
      })));

      // Do NOT await any promises from pui

      // The PromisesUnordered will be destroyed at the end of this scope,
      // which should trigger the destruction of all tasks
    }
    // When pui is destroyed, it will destroy all its tasks,
    // capture the exception, and re-throw it
  });
}

KJ_TEST("PromisesUnordered handles multiple task destruction exceptions when destroyed") {
  // This test verifies that when multiple task destructors throw exceptions,
  // only the first exception is propagated

  // Tasks are destroyed in LIFO order (last in, first out), so the second task
  // will be destroyed first and its exception will be caught first
  KJ_EXPECT_THROW_MESSAGE("Second task destructor error", {
    EventLoop loop;
    WaitScope waitScope(loop);

    {
      PromisesUnordered<int> pui;

      // Add multiple promises that throw exceptions when destroyed,
      // they will be destroyed in LIFO order (reverse of the order they were added)

      // First task with a distinct error message (will be destroyed second)
      pui.add(kj::Promise<int>(123).attach(kj::defer([] {
        KJ_FAIL_ASSERT("First task destructor error");
      })));

      // Second task with a different error message (will be destroyed first)
      pui.add(kj::Promise<int>(456).attach(kj::defer([] {
        KJ_FAIL_ASSERT("Second task destructor error");
      })));

      // Do NOT await any promises from pui

      // The PromisesUnordered will be destroyed at the end of this scope,
      // which will destroy tasks in LIFO order (last added, first destroyed)
    }
    // When pui is destroyed, it should capture and propagate the first exception thrown,
    // which will be from the second task since it was added last and destroyed first
  });
}

// ======================================================================================
// Destruction and Safety
// ======================================================================================

KJ_TEST("PromisesUnordered handles destruction of many pending tasks") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // We'll create this in a separate scope so it's destroyed
  {
    // Create a PromisesUnordered with a large number of never-done promises
    // This would cause a stack overflow during destruction if we didn't handle it properly
    PromisesUnordered<int> pui;

    // Add many tasks - 100,000 should be enough to cause a stack overflow
    // with recursive destruction if we don't handle it properly
    const uint taskCount = 100000;

    for (uint i = 0; i < taskCount; i++) {
      pui.add(kj::Promise<int>(kj::NEVER_DONE));
    }

    // Now the PromisesUnordered will be destroyed at the end of this scope
    // Our custom destructor should prevent stack overflow
  }

  // If we reach here without a stack overflow, the test passed
}

KJ_TEST("PromisesUnordered aborts when destroyed before its begin promise") {
  // This test verifies that when a PromisesUnordered is destroyed while its begin promise
  // still exists, it aborts the process immediately

  // We use KJ_EXPECT_SIGNAL to test that the process is terminated with SIGABRT
  KJ_EXPECT_SIGNAL(SIGABRT, {
    // Set up log expectation inside the signal handler block
    KJ_EXPECT_LOG(FATAL, "PromisesUnordered destroyed while a promise from begin() still exists");

    EventLoop loop;
    WaitScope waitScope(loop);

    // Hold a promise outside the scope of the PromisesUnordered
    Promise<int> capturedBeginPromise = nullptr;

    {
      // Create a PromisesUnordered in a scope that will end
      PromisesUnordered<int> pui;

      // Add a promise that will never resolve
      pui.add(kj::Promise<int>(kj::NEVER_DONE));

      // Begin iterator returns a promise we'll keep beyond pui's lifetime
      capturedBeginPromise = *pui.begin();

      // pui will be destroyed at the end of this scope, which should trigger an abort
    }

    // This line should never be reached, as destruction of pui should have aborted
    KJ_FAIL_ASSERT("This code should not be reached");
  });
}

// ======================================================================================
// Cancellation and Promise Lifetime
// ======================================================================================

KJ_TEST("PromisesUnordered begin promise cancellation doesn't cancel promises in collection") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // Create flags to track if promises were canceled
  bool promise1Canceled = false;
  bool promise2Canceled = false;

  // Create a PromisesUnordered collection
  PromisesUnordered<int> pui;

  // Add two promises that will track if they're canceled
  auto paf1 = kj::newPromiseAndFulfiller<int>();
  auto paf2 = kj::newPromiseAndFulfiller<int>();

  // Add a promise that will become ready on the next event loop turn
  pui.add(kj::evalLater([]() { return 42; }));

  // Add promises with cancellation detection
  pui.add(paf1.promise.attach(kj::defer([&promise1Canceled, &fulfiller = *paf1.fulfiller]() {
    // This will run when the promise is destroyed - due to either
    // cancellation or normal completion
    if (!fulfiller.isWaiting()) {
      // The promise was already fulfilled or rejected
      return;
    }
    // If we get here, the promise was destroyed while still unfulfilled,
    // which means it was canceled
    promise1Canceled = true;
  })));

  pui.add(paf2.promise.attach(kj::defer([&promise2Canceled, &fulfiller = *paf2.fulfiller]() {
    // This will run when the promise is destroyed - due to either
    // cancellation or normal completion
    if (!fulfiller.isWaiting()) {
      // The promise was already fulfilled or rejected
      return;
    }
    // If we get here, the promise was destroyed while still unfulfilled,
    // which means it was canceled
    promise2Canceled = true;
  })));

  // Create a custom event to track when it's fired
  class TestEvent: public _::Event {
  public:
    TestEvent(): _::Event(SourceLocation()) {}

    bool fired = false;

    Maybe<Own<_::Event>> fire() override {
      fired = true;
      return kj::none;
    }

    void traceEvent(kj::_::TraceBuilder& builder) override {
      // Don't add anything to the builder
    }
  };

  // Create a custom event that will be triggered when a promise is ready
  TestEvent testEvent;

  {
    // Extract the promise node directly, moving the promise
    auto ownNode = _::PromiseNode::from(*pui.begin());

    // Set self pointer (to match what the Promise wrapper does)
    ownNode->setSelfPointer(&ownNode);

    // Register our test event with the node
    ownNode->onReady(&testEvent);

    // Run the event loop once to allow events to be processed
    loop.run();

    // Verify our event was fired, indicating the promise was ready
    KJ_EXPECT(testEvent.fired);

    // Now explicitly cancel by destroying ownNode (as the Promise destructor would)
  }

  // Verify that neither promise was canceled
  KJ_EXPECT(!promise1Canceled);
  KJ_EXPECT(!promise2Canceled);

  // Now fulfill both promises to ensure the test cleans up correctly
  paf1.fulfiller->fulfill(1);
  paf2.fulfiller->fulfill(2);

  // Check the promises in fulfillment order
  auto it = pui.begin();
  KJ_EXPECT((*it).wait(waitScope) == 42);  // evalLater() promise completes first

  ++it;
  KJ_EXPECT((*it).wait(waitScope) == 1);  // First fulfiller promise

  ++it;
  KJ_EXPECT((*it).wait(waitScope) == 2);  // Second fulfiller promise

  ++it;
  KJ_EXPECT(it == pui.end());  // All promises consumed
}

// ======================================================================================
// Tracing
// ======================================================================================

KJ_TEST("PromisesUnordered tracing works correctly") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // Create a PromisesUnordered with multiple promises in various states
  PromisesUnordered<int> pui;

  // 1. Add a fulfilled promise with a value
  pui.add(kj::Promise<int>(42));

  // 2. Add a promise with a chain of transformations for more interesting traces
  auto evaluatedPromise = kj::evalLater([]() -> int {
    return 123;
  }).then([](int value) {
    return value * 2;
  });
  pui.add(kj::mv(evaluatedPromise));

  // 3. Add a never-done promise
  pui.add(kj::Promise<int>(kj::NEVER_DONE));

  // Get an iterator and dereference it to get a promise
  Promise<int> readyPromise = *pui.begin();

  // Get a trace of the promise from the begin() iterator
  String readyPromiseTrace = readyPromise.trace();

  // The trace string should not be empty
  KJ_ASSERT(readyPromiseTrace.size() > 0);

  // Execute one iteration to consume a promise
  readyPromise.wait(waitScope);

  // After consuming a promise, get the trace of another one
  if (!pui.isEmpty()) {
    Promise<int> secondPromise = *pui.begin();
    String secondTrace = secondPromise.trace();

    // The second trace should also not be empty
    KJ_ASSERT(secondTrace.size() > 0);

    // Consume this promise too
    secondPromise.wait(waitScope);
  }

  // Verify tracing with ReadyTaskPromiseNode forwarding to a never-done promise
  if (!pui.isEmpty()) {
    Promise<int> neverDonePromise = *pui.begin();
    String neverDoneTrace = neverDonePromise.trace();

    // Should contain trace information for the never-done promise
    KJ_ASSERT(neverDoneTrace.size() > 0);
  }
}

// Structure for storing trace analysis results with well-named members
struct TraceCount {
  int pending = 0;
  int ready = 0;

  // Total number of promises (both pending and ready)
  int total() const { return pending + ready; }
};

// Helper function to count the number of pending and ready lines in a trace
TraceCount countPendingAndReady(kj::StringPtr trace) {
  TraceCount result;

  std::stringstream ss(trace.cStr());
  std::string line;

  while (std::getline(ss, line)) {
    if (!line.empty()) {
      // Check if the line starts with the expected prefixes
      if (line.starts_with("pending task:")) {
        result.pending++;
      } else if (line.starts_with("ready task:")) {
        result.ready++;
      }
    }
  }
  return result;
}

KJ_TEST("PromisesUnordered::trace() with different numbers of promises") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // Case 1: Empty collection
  {
    PromisesUnordered<int> empty;
    String trace = empty.trace();

    // Trace of empty collection should be empty string
    KJ_EXPECT(trace.size() == 0);
  }

  // Case 2: Collection with one pending promise
  {
    PromisesUnordered<int> pui;
    pui.add(kj::Promise<int>(42));

    String trace = pui.trace();

    // Should have trace information for one pending promise
    KJ_EXPECT(trace.size() > 0);

    // Count pending and ready lines
    TraceCount counts = countPendingAndReady(trace);

    // Should have exactly one pending promise and no ready promises
    KJ_EXPECT(counts.pending == 1);
    KJ_EXPECT(counts.ready == 0);
  }

  // Case 3: Collection with one pending and one ready
  {
    PromisesUnordered<int> pui;

    // Add a never-ready promise
    pui.add(kj::Promise<int>(kj::NEVER_DONE));

    // Add an immediately ready promise
    pui.add(kj::Promise<int>(42));

    // Get an iterator but don't wait on it
    Promise<int> readyPromise = *pui.begin();

    // Use poll() to drive execution without consuming the task
    readyPromise.poll(waitScope);

    // Now we should have one ready and one pending
    String trace = pui.trace();

    // Count pending and ready lines
    TraceCount counts = countPendingAndReady(trace);

    // Should have exactly one pending and one ready promise
    KJ_EXPECT(counts.pending == 1);
    KJ_EXPECT(counts.ready == 1);
  }

  // Case 4: Collection with many promises of mixed states
  {
    PromisesUnordered<int> pui;

    // Add a bunch of promises
    for (int i = 0; i < 10; i++) {
      // Half are ready immediately, half are pending
      if (i % 2 == 0) {
        pui.add(kj::Promise<int>(i));
      } else {
        pui.add(kj::Promise<int>(kj::NEVER_DONE));
      }
    }

    // Drive execution to move promises to ready state, but don't consume them
    Promise<int> readyPromise = *pui.begin();

    // Poll once to start resolving promises
    readyPromise.poll(waitScope);

    // Poll multiple times to ensure promises have time to transition states
    for (int i = 0; i < 5; i++) {
      readyPromise.poll(waitScope);
    }

    String trace = pui.trace();

    // Count pending and ready lines
    TraceCount counts = countPendingAndReady(trace);

    // Print the current state for debugging
    KJ_LOG(INFO, "After polling multiple times:", counts.pending, counts.ready, counts.total());

    // Should have 5 pending and 5 ready tasks
    KJ_EXPECT(counts.pending == 5);
    KJ_EXPECT(counts.ready == 5);
    KJ_EXPECT(counts.total() == 10);
  }
}

// ======================================================================================
// isEmpty() and never-done promises
// ======================================================================================

KJ_TEST("PromisesUnordered detects when a promise from begin() already exists") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add a never-done promise to ensure pui.isEmpty() returns false
  pui.add(kj::Promise<int>(kj::NEVER_DONE));

  // Get an iterator and dereference it to get the first promise
  auto it = pui.begin();
  Promise<int> firstPromise = *it;

  // Before consuming the first promise, try to get another one
  // This returns a rejected promise that will throw when waited on
  Promise<int> secondPromise = *pui.begin();

  // Waiting on this second promise should throw the expected exception
  KJ_EXPECT_THROW_MESSAGE(
      "A Promise for the next ready task already exists",
      secondPromise.wait(waitScope));

  // We should also get the same error from the original iterator if we dereference it again
  Promise<int> reusedPromise = *it;
  KJ_EXPECT_THROW_MESSAGE(
      "A Promise for the next ready task already exists",
      reusedPromise.wait(waitScope));

  // Once we finish with the first promise (by dropping it),
  // we should be able to get a new one
  firstPromise = nullptr;

  // Now we can get a new promise from begin()
  auto newIt = pui.begin();
  KJ_EXPECT(newIt != pui.end()); // Should not be at the end since we have a never-done promise

  // Should be able to get a new promise without throwing
  Promise<int> validPromise = *newIt;

  // We can't wait on this promise since it's a never-done promise, but we can trace it
  String trace = validPromise.trace();
  KJ_EXPECT(trace.size() > 0);

  // The collection is still not empty because of the never-done promise
  KJ_EXPECT(!pui.isEmpty());
}

KJ_TEST("PromisesUnordered detects operations on end iterator") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Get the end iterator
  auto endIt = pui.end();

  // Attempting to dereference the end iterator should throw an exception
  // with a clear error message
  KJ_EXPECT_THROW_MESSAGE(
      "Cannot dereference end iterator",
      *endIt);

  // Attempting to increment the end iterator should also throw an exception
  KJ_EXPECT_THROW_MESSAGE(
      "Cannot increment end iterator",
      ++endIt);
}

KJ_TEST("PromisesUnordered detects empty collection on iterator dereference") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Add a single ready promise
  pui.add(kj::Promise<int>(42));

  // Save the iterator returned by begin()
  auto it = pui.begin();

  // Get the promise from this iterator and await it
  {
    Promise<int> promise = *it;
    int value = promise.wait(waitScope);
    KJ_EXPECT(value == 42);
  }

  // Now the collection should be empty
  KJ_EXPECT(pui.isEmpty());

  // Dereferencing the same iterator again should return a rejected promise
  Promise<int> emptyPromise = *it;

  // Waiting on this promise should throw the "No promises in the collection" exception
  KJ_EXPECT_THROW_MESSAGE(
      "No promises in the collection",
      emptyPromise.wait(waitScope));
}

KJ_TEST("PromisesUnordered isEmpty returns false with never-done promise") {
  EventLoop loop;
  WaitScope waitScope(loop);

  PromisesUnordered<int> pui;

  // Initially pui should be empty
  KJ_EXPECT(pui.isEmpty());

  // Add a never-done promise to ensure pui.isEmpty() returns false
  pui.add(kj::Promise<int>(kj::NEVER_DONE));

  // Verify that the collection is not empty because of the never-done promise
  KJ_EXPECT(!pui.isEmpty());

  // Add a ready promise
  pui.add(kj::Promise<int>(42));

  // We should be able to get this promise through iteration
  {
    Promise<int> promise = *pui.begin();
    int value = promise.wait(waitScope);
    KJ_EXPECT(value == 42);
  }

  // After consuming the ready promise, the collection should still not be empty
  // because of the never-done promise
  KJ_EXPECT(!pui.isEmpty());

  // Add another ready promise
  pui.add(kj::Promise<int>(99));

  // Process this promise too
  {
    Promise<int> promise = *pui.begin();
    int value = promise.wait(waitScope);
    KJ_EXPECT(value == 99);
  }

  // The collection should still not be empty because of the never-done promise
  KJ_EXPECT(!pui.isEmpty());
}

}  // namespace
}  // namespace kj
