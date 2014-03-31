// Copyright (c) 2014, Kenton Varda <temporal@gmail.com>
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

class MicrosecondLabel;

}  // namespace _ (private)

using Time = Quantity<int64_t, _::MicrosecondLabel>;
// A time value, in microseconds.

constexpr Time MICROSECOND = unit<Time>();
constexpr Time MILLISECOND = 1000 * MICROSECOND;
constexpr Time SECOND = 1000 * MILLISECOND;
constexpr Time MINUTE = 60 * SECOND;
constexpr Time HOUR = 60 * MINUTE;
constexpr Time DAY = 24 * HOUR;

class Timer {
  // Interface to time and timer functionality. The underlying time unit comes from
  // a steady clock, i.e. a clock that increments steadily and is independent of
  // system (or wall) time.

public:
  virtual Time steadyTime() = 0;
  // Returns the current value of a clock that moves steadily forward, independent of any
  // changes in the wall clock. The value is updated every time the event loop waits,
  // and is constant in-between waits.

  virtual Promise<void> atSteadyTime(Time time) = 0;
  // Schedules a timer that will trigger when the value returned by steadyTime() reaches
  // the given time.

  virtual Promise<void> atTimeFromNow(Time delay) = 0;
  // Schedules a timer that will trigger after the given delay. The timer uses steadyTime()
  // and is therefore immune to system clock updates.
};

}  // namespace kj

#endif  // KJ_TIME_H_
