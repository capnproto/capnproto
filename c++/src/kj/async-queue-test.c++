// Copyright (c) 2021 Cloudflare, Inc. and contributors
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

#include "async-queue.h"

#include <kj/async-io.h>
#include <kj/test.h>
#include <kj/vector.h>

namespace kj {
namespace {

struct QueueTest {
  kj::AsyncIoContext io = setupAsyncIo();
  ProducerConsumerQueue<size_t> queue;

  QueueTest() = default;
  QueueTest(QueueTest&&) = delete;
  QueueTest(const QueueTest&) = delete;
  QueueTest& operator=(QueueTest&&) = delete;
  QueueTest& operator=(const QueueTest&) = delete;

  struct Producer {
    QueueTest& test;
    Promise<void> promise = kj::READY_NOW;

    Producer(QueueTest& test): test(test) {}

    void push(size_t i) {
      auto push = [&, i]() -> Promise<void> {
        test.queue.push(i);
        return kj::READY_NOW;
      };
      promise = promise.then(kj::mv(push));
    }
  };

  struct Consumer {
    QueueTest& test;
    Promise<void> promise = kj::READY_NOW;

    Consumer(QueueTest& test): test(test) {}

    void pop(Vector<bool>& bits) {
      auto pop = [&]() {
        return test.queue.pop();
      };
      auto checkPop = [&](size_t j) -> Promise<void> {
        bits[j] = true;
        return kj::READY_NOW;
      };
      promise = promise.then(kj::mv(pop)).then(kj::mv(checkPop));
    }
  };
};

KJ_TEST("ProducerConsumerQueue with various amounts of producers and consumers") {
  QueueTest test;

  size_t constexpr kItemCount = 1000;
  for (auto producerCount: { 1, 5, 10 }) {
    for (auto consumerCount: { 1, 5, 10 }) {
      KJ_LOG(INFO, "Testing a new set of Producers and Consumers",  //
             producerCount, consumerCount, kItemCount);
      // Make a vector to track our entries.
      auto bits = Vector<bool>(kItemCount);
      for (auto i KJ_UNUSED : kj::zeroTo(kItemCount)) {
        bits.add(false);
      }

      // Make enough producers.
      auto producers = Vector<QueueTest::Producer>();
      for (auto i KJ_UNUSED : kj::zeroTo(producerCount)) {
        producers.add(test);
      }

      // Make enough consumers.
      auto consumers = Vector<QueueTest::Consumer>();
      for (auto i KJ_UNUSED : kj::zeroTo(consumerCount)) {
        consumers.add(test);
      }

      for (auto i : kj::zeroTo(kItemCount)) {
        // Use a producer and a consumer for each entry.

        auto& producer = producers[i % producerCount];
        producer.push(i);

        auto& consumer = consumers[i % consumerCount];
        consumer.pop(bits);
      }

      // Confirm that all entries are produced and consumed.
      auto promises = Vector<Promise<void>>();
      for (auto& producer: producers) {
        promises.add(kj::mv(producer.promise));
      }
      for (auto& consumer: consumers) {
        promises.add(kj::mv(consumer.promise));
      }
      joinPromises(promises.releaseAsArray()).wait(test.io.waitScope);
      for (auto i : kj::zeroTo(kItemCount)) {
        KJ_ASSERT(bits[i], i);
      }
    }
  }
}

KJ_TEST("ProducerConsumerQueue with rejectAll()") {
  QueueTest test;

  for (auto consumerCount: { 1, 5, 10 }) {
    KJ_LOG(INFO, "Testing a new set of consumers with rejection", consumerCount);

    // Make enough consumers.
    auto promises = Vector<Promise<void>>();
    for (auto i KJ_UNUSED : kj::zeroTo(consumerCount)) {
      promises.add(test.queue.pop().ignoreResult());
    }

    for (auto& promise: promises) {
      KJ_EXPECT(!promise.poll(test.io.waitScope), "All of our consumers should be waiting");
    }
    test.queue.rejectAll(KJ_EXCEPTION(FAILED, "Total rejection"));

    // We should have finished and swallowed the errors.
    auto promise = joinPromises(promises.releaseAsArray());
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("Total rejection", promise.wait(test.io.waitScope));
  }
}

}  // namespace
}  // namespace kj
