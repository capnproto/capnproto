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

#include "sticky-flag.h"

#include <kj/debug.h>
#include <kj/list.h>

namespace kj {

class StickyFlag::BaseWaiter {
 public:
  explicit BaseWaiter(PromiseFulfiller<void>& fulfiller) : fulfiller(fulfiller) {}

  PromiseFulfiller<void>& fulfiller;
  ListLink<BaseWaiter> link;
};

struct StickyFlag::State {
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

class StickyFlag::Waiter final: public BaseWaiter {
 public:
  Waiter(PromiseFulfiller<void>& fulfiller, Rc<State> state)
      : BaseWaiter(fulfiller), state(kj::mv(state)) {
    this->state->waiters.add(*this);
  }

  ~Waiter() noexcept(false) {
    if (link.isLinked())
      state->waiters.remove(*this);
  }

 private:
  Rc<State> state;
};

StickyFlag::StickyFlag(): state(rc<State>()) {}

StickyFlag::~StickyFlag() noexcept(false) {
  // Will only reject if not already resolved.
  reject(KJ_EXCEPTION(FAILED, "StickyFlag destroyed before it was signaled or rejected"));
}

Promise<void> StickyFlag::whenSignaled() {
  if (!state->ready) {
    return newAdaptedPromise<void, Waiter>(state.addRef());
  }
  KJ_IF_SOME(exception, state->exception) {
    return exception.clone();
  }
  return READY_NOW;
}

bool StickyFlag::signal() {
  return state->signal();
}

bool StickyFlag::reject(Exception exception) {
  return state->reject(kj::mv(exception));
}

}  // namespace kj
