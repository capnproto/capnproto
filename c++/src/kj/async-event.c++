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

#include "async-event.h"

#include <kj/debug.h>
#include <kj/list.h>

namespace kj {

class AsyncEvent::BaseWaiter {
 public:
  explicit BaseWaiter(PromiseFulfiller<void>& fulfiller);

  PromiseFulfiller<void>& fulfiller;
  ListLink<BaseWaiter> link;
};

class AsyncEvent::Waiter final: public BaseWaiter {
 public:
  Waiter(PromiseFulfiller<void>& fulfiller, Rc<State> state);
  ~Waiter() noexcept(false);

 private:
  Rc<State> state;
};

struct AsyncEvent::State {
  bool signal() {
    if (ready) return false;
    ready = true;

    while (!waiters.empty()) {
      auto& waiter = waiters.front();
      waiters.remove(waiter);
      waiter.fulfiller.fulfill();
    }
    return true;
  }

  bool reject(Exception exception) {
    if (ready) return false;
    this->exception = kj::mv(exception);
    ready = true;

    while (!waiters.empty()) {
      auto& waiter = waiters.front();
      waiters.remove(waiter);
      waiter.fulfiller.reject(KJ_ASSERT_NONNULL(this->exception).clone());
    }
    return true;
  }

  bool ready = false;
  Maybe<Exception> exception;
  List<BaseWaiter, &BaseWaiter::link> waiters;
};

AsyncEvent::BaseWaiter::BaseWaiter(PromiseFulfiller<void>& fulfiller): fulfiller(fulfiller) {}

AsyncEvent::Waiter::Waiter(PromiseFulfiller<void>& fulfiller, Rc<State> state)
    : BaseWaiter(fulfiller),
      state(kj::mv(state)) {
  this->state->waiters.add(*this);
}

AsyncEvent::Waiter::~Waiter() noexcept(false) {
  if (link.isLinked()) state->waiters.remove(*this);
}

AsyncEvent::AsyncEvent(): state(rc<State>()) {}

AsyncEvent::~AsyncEvent() noexcept(false) {
  // Will only reject if not already resolved.
  reject(KJ_EXCEPTION(FAILED, "AsyncEvent destroyed before it was signaled or rejected"));
}

Promise<void> AsyncEvent::whenSignaled() {
  if (!state->ready) {
    return newAdaptedPromise<void, Waiter>(state.addRef());
  }
  KJ_IF_SOME(exception, state->exception) {
    return exception.clone();
  }
  return READY_NOW;
}

bool AsyncEvent::signal() {
  return state->signal();
}

bool AsyncEvent::reject(Exception exception) {
  return state->reject(kj::mv(exception));
}

}  // namespace kj
