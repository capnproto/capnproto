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
  TimerPromiseAdapter(PromiseFulfiller<void>& fulfiller, TimerImpl& parent, TimePoint time)
      : time(time), fulfiller(fulfiller), parent(parent) {
    pos = parent.impl->timers.insert(this);

    KJ_IF_SOME(h, parent.sleepHooks) {
      if (pos == parent.impl->timers.begin()) {
        h.updateNextTimerEvent(time);
      }
    }
  }

  ~TimerPromiseAdapter() {
    if (pos != parent.impl->timers.end()) {
      KJ_IF_SOME(h, parent.sleepHooks) {
        bool isFirst = pos == parent.impl->timers.begin();

        parent.impl->timers.erase(pos);

        if (isFirst) {
          if (parent.impl->timers.empty()) {
            h.updateNextTimerEvent(kj::none);
          } else {
            h.updateNextTimerEvent((*parent.impl->timers.begin())->time);
          }
        }
      } else {
        parent.impl->timers.erase(pos);
      }
    }
  }

  void fulfill() {
    fulfiller.fulfill();
    parent.impl->timers.erase(pos);
    pos = parent.impl->timers.end();
  }

  const TimePoint time;

private:
  PromiseFulfiller<void>& fulfiller;
  TimerImpl& parent;
  Impl::Timers::const_iterator pos;
};

inline bool TimerImpl::Impl::TimerBefore::operator()(
    TimerPromiseAdapter* lhs, TimerPromiseAdapter* rhs) const {
  return lhs->time < rhs->time;
}

TimePoint TimerImpl::now() const {
  KJ_IF_SOME(h, sleepHooks) {
    return h.getTimeWhileSleeping();
  } else {
    return time;
  }
}

Promise<void> TimerImpl::atTime(TimePoint time) {
  auto result = newAdaptedPromise<void, TimerPromiseAdapter>(*this, time);
  return result;
}

Promise<void> TimerImpl::afterDelay(Duration delay) {
  return newAdaptedPromise<void, TimerPromiseAdapter>(*this, now() + delay);
}

TimerImpl::TimerImpl(TimePoint startTime)
    : time(startTime), impl(heap<Impl>()) {}

TimerImpl::~TimerImpl() noexcept(false) {}

Maybe<TimePoint> TimerImpl::nextEvent() {
  auto iter = impl->timers.begin();
  if (iter == impl->timers.end()) {
    return kj::none;
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
  sleepHooks = nullptr;

  // On Macs running an Intel processor, it has been observed that clock_gettime
  // may return non monotonic time, even when CLOCK_MONOTONIC is used.
  // This workaround is to avoid the assert triggering on these machines.
  // See also https://github.com/capnproto/capnproto/issues/1693
#if __APPLE__ && (defined(__x86_64__) || defined(__POWERPC__))
  time = std::max(time, newTime);
#else
  KJ_REQUIRE(newTime >= time, "can't advance backwards in time") { return; }
  time = newTime;
#endif

  for (;;) {
    auto front = impl->timers.begin();
    if (front == impl->timers.end() || (*front)->time > time) {
      break;
    }
    (*front)->fulfill();
  }
}

}  // namespace kj
