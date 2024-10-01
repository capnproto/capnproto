// Copyright (c) 2014 Google Inc. (contributed by Remy Blank <rblank@google.com>)
// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "timer.h"
#include "debug.h"
#include <set>

namespace kj {

kj::Exception Timer::makeTimeoutException() {
  return KJ_EXCEPTION(OVERLOADED, "operation timed out");
}

struct TimerImpl::Impl {
  struct TimerBefore {
    bool operator()(TimerPromiseAdapter* lhs, TimerPromiseAdapter* rhs) const;
  };
  using Timers = std::multiset<TimerPromiseAdapter*, TimerBefore>;
  Timers timers;
};

class TimerImpl::TimerPromiseAdapter {
public:
  TimerPromiseAdapter(PromiseFulfiller<void>& fulfiller, TimerImpl::Impl& impl, TimePoint time)
      : time(time), fulfiller(fulfiller), impl(impl) {
    pos = impl.timers.insert(this);
  }

  ~TimerPromiseAdapter() {
    if (pos != impl.timers.end()) {
      impl.timers.erase(pos);
    }
  }

  void fulfill() {
    fulfiller.fulfill();
    impl.timers.erase(pos);
    pos = impl.timers.end();
  }

  const TimePoint time;

private:
  PromiseFulfiller<void>& fulfiller;
  TimerImpl::Impl& impl;
  Impl::Timers::const_iterator pos;
};

inline bool TimerImpl::Impl::TimerBefore::operator()(
    TimerPromiseAdapter* lhs, TimerPromiseAdapter* rhs) const {
  return lhs->time < rhs->time;
}

Promise<void> TimerImpl::atTime(TimePoint time) {
  return newAdaptedPromise<void, TimerPromiseAdapter>(*impl, time);
}

Promise<void> TimerImpl::afterDelay(Duration delay) {
  return newAdaptedPromise<void, TimerPromiseAdapter>(*impl, time + delay);
}

TimerImpl::TimerImpl(TimePoint startTime)
    : time(startTime), impl(heap<Impl>()) {}

TimerImpl::~TimerImpl() noexcept(false) {}

Maybe<TimePoint> TimerImpl::nextEvent() {
  auto iter = impl->timers.begin();
  if (iter == impl->timers.end()) {
    return nullptr;
  } else {
    return (*iter)->time;
  }
}

Maybe<uint64_t> TimerImpl::timeoutToNextEvent(TimePoint start, Duration unit, uint64_t max) {
  return nextEvent().map([&](TimePoint nextTime) -> uint64_t {
    if (nextTime <= start) return 0;

    Duration timeout = nextTime - start;

    uint64_t result = timeout / unit;
    bool roundUp = timeout % unit > 0 * SECONDS;

    if (result >= max) {
      return max;
    } else {
      return result + roundUp;
    }
  });
}

void TimerImpl::advanceTo(TimePoint newTime) {
  // It has been observed that clock_gettime 
  // may return non monotonic time, even when CLOCK_MONOTONIC is used.
  // We use std::max to guard against this rare issue.
  // - on mac: https://github.com/capnproto/capnproto/issues/1693
  // - on linux: https://github.com/capnproto/capnproto/issues/2261
  time = std::max(time, newTime);

  for (;;) {
    auto front = impl->timers.begin();
    if (front == impl->timers.end() || (*front)->time > time) {
      break;
    }
    (*front)->fulfill();
  }
}

}  // namespace kj
