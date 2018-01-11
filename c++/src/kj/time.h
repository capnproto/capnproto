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

#pragma once

#if defined(__GNUC__) && !KJ_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include "units.h"
#include <inttypes.h>

namespace kj {
namespace _ {  // private

class NanosecondLabel;
class TimeLabel;
class DateLabel;

}  // namespace _ (private)

using Duration = Quantity<int64_t, _::NanosecondLabel>;
// A time value, in nanoseconds.

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

class Clock {
  // Interface to read the current date and time.
public:
  virtual Date now() const = 0;
};

const Clock& nullClock();
// A clock which always returns UNIX_EPOCH as the current time. Useful when you don't care about
// time.

}  // namespace kj
