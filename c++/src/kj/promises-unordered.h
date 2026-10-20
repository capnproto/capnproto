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

#pragma once

#include "async-queue.h"

KJ_BEGIN_HEADER

namespace kj {

template <typename T>
class PromisesUnordered final: private TaskSet::ErrorHandler {
  // Owns a collection of promises and yields their results in completion order.
  //
  // Like TaskSet, PromisesUnordered begins evaluating promises when they are added and cancels
  // unfinished promises when destroyed. Unlike TaskSet, which discards successful results and
  // passes failures to an ErrorHandler, PromisesUnordered retains every value or failure for the
  // caller to consume with pop(). A failed input is returned as a broken Promise<T> without
  // affecting the other inputs.
  //
  // At most one promise returned by pop() may be outstanding at a time.

public:
  PromisesUnordered(SourceLocation location = {}): tasks(*this, location) {}
  // Construct an empty collection. `location` is used in asynchronous stack traces for its tasks.

  ~PromisesUnordered() noexcept(false) {
    results.rejectAll(KJ_EXCEPTION(DISCONNECTED, "PromisesUnordered was destroyed"));
  }
  // Cancel all unfinished promises and discard any results that have not been consumed. A promise
  // returned by pop() that is still waiting for a result is rejected.

  KJ_DISALLOW_COPY_AND_MOVE(PromisesUnordered);

  void add(Promise<T>&& promise) {
    // Input failures propagate to taskFailed(), so every failure follows the same queueing path.
    auto task = kj::mv(promise).then(PushResult{*this});
    tasks.add(kj::mv(task));
    ++state->outstandingCount;
  }
  // Add `promise` to the collection and begin evaluating it.

  size_t size() const {
    return state->outstandingCount;
  }
  // Return the number of unfinished promises and completed results that have not yet been delivered
  // by pop().

  bool isEmpty() const {
    return size() == 0;
  }
  // Return true if the collection has neither unfinished promises nor unconsumed results.

  Promise<T> pop() {
    if (isEmpty()) {
      return KJ_EXCEPTION(FAILED, "no promises in collection");
    }
    if (state->waiterOutstanding) {
      return KJ_EXCEPTION(FAILED, "a promise returned by pop() already exists");
    }

    state->waiterOutstanding = true;
    auto guard = heap<WaitGuard>(kj::addRef(*state));
    return results.pop().then(PopResult{kj::mv(guard)});
  }
  // Return a promise for the next input to complete. Only one such promise may be outstanding at a
  // time. Canceling it before it resolves does not cancel any input promise. Calling pop() on an
  // empty collection returns a broken promise.

  kj::String trace() {
    return tasks.trace();
  }
  // Return asynchronous stack traces for the promises that are still executing. Results that have
  // already completed and are waiting to be consumed are not included.

private:
  struct State final: public Refcounted {
    size_t outstandingCount = 0;
    bool waiterOutstanding = false;
  };

  struct Result {
    Result(Promise<T>&& promise, Own<State>&& state)
        : promise(kj::mv(promise)), state(kj::mv(state)) {}
    Result(Result&&) = default;
    Result& operator=(Result&&) = delete;
    KJ_DISALLOW_COPY(Result);

    ~Result() noexcept(false) {
      if (state.get() != nullptr) {
        KJ_ASSERT(state->outstandingCount > 0);
        --state->outstandingCount;
      }
    }

    Promise<T> promise;
    Own<State> state;
  };

  struct WaitGuard {
    explicit WaitGuard(Own<State>&& state): state(kj::mv(state)) {}

    ~WaitGuard() noexcept {
      state->waiterOutstanding = false;
    }

    Own<State> state;
  };

  struct PopResult {
    Own<WaitGuard> guard;

    Promise<T> operator()(Result&& result) {
      guard = nullptr;
      return kj::mv(result.promise);
    }
  };

  struct PushResult {
    PromisesUnordered& collection;

    void operator()(_::FixVoid<T>&& value) {
      collection.pushResult(Promise<T>(kj::mv(value)));
    }

    void operator()() {
      collection.pushResult(Promise<T>(READY_NOW));
    }
  };

  void pushResult(Promise<T>&& promise) {
    results.push(Result(kj::mv(promise), kj::addRef(*state)));
  }

  void taskFailed(Exception&& exception) override {
    pushResult(Promise<T>(kj::mv(exception)));
  }

  Own<State> state = refcounted<State>();
  ProducerConsumerQueue<Result> results;
  TaskSet tasks;
};

}  // namespace kj

KJ_END_HEADER
