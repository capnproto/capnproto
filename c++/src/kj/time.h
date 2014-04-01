// Copyright (c) 2014 Google Inc. (contributed by Remy Blank <rblank@google.com>)
// Copyright (c) 2014 Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef KJ_TIME_H_
#define KJ_TIME_H_

#include "async.h"
#include "units.h"
#include <inttypes.h>

namespace kj {
namespace _ {  // private

class NanosecondLabel;
class TimeLabel;
class DateLabel;

}  // namespace _ (private)

using Duration = Quantity<int64_t, _::NanosecondLabel>;
// A time value, in microseconds.

constexpr Duration NANOSECONDS = unit<Duration>();
constexpr Duration MICROSECONDS = 1000 * NANOSECONDS;
constexpr Duration MILLISECONDS = 1000 * MICROSECONDS;
constexpr Duration SECONDS = 1000 * MILLISECONDS;
constexpr Duration MINUTES = 60 * SECONDS;
constexpr Duration HOURS = 60 * MINUTES;
constexpr Duration DAYS = 24 * HOURS;

using TimePoint = Absolute<Duration, _::TimeLabel>;
// An absolute time measured by some particular instance of `Timer`.  `Time`s from two different
// `Timer`s may be measured from different origins and so are not necessarily compatible.

using Date = Absolute<Duration, _::DateLabel>;
// A point in real-world time, measured relative to the Unix epoch (Jan 1, 1970 00:00:00 UTC).

constexpr Date UNIX_EPOCH = origin<Date>();
// The `Date` representing Jan 1, 1970 00:00:00 UTC.

class Timer {
  // Interface to time and timer functionality.
  //
  // Each `Timer` may have a different origin, and some `Timer`s may in fact tick at a different
  // rate than real time (e.g. a `Timer` could represent CPU time consumed by a thread).  However,
  // all `Timer`s are monotonic: time will never appear to move backwards, even if the calendar
  // date as tracked by the system is manually modified.

public:
  virtual TimePoint now() = 0;
  // Returns the current value of a clock that moves steadily forward, independent of any
  // changes in the wall clock. The value is updated every time the event loop waits,
  // and is constant in-between waits.

  virtual Promise<void> atTime(TimePoint time) = 0;
  // Returns a promise that returns as soon as now() >= time.

  virtual Promise<void> afterDelay(Duration delay) = 0;
  // Equivalent to atTime(now() + delay).
};

}  // namespace kj

#endif  // KJ_TIME_H_
