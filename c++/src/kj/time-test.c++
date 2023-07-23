// Copyright (c) 2019 Cloudflare, Inc. and contributors
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

#if _WIN32
#include "win32-api-version.h"
#endif

#include "time.h"
#include "debug.h"
#include <kj/test.h>
#include <time.h>

#if _WIN32
#include <windows.h>
#include "windows-sanity.h"
#else
#include <unistd.h>
#endif

namespace kj {
namespace {

KJ_TEST("stringify times") {
  KJ_EXPECT(kj::str(50 * kj::SECONDS) == "50s");
  KJ_EXPECT(kj::str(5 * kj::SECONDS + 2 * kj::MILLISECONDS) == "5.002s");
  KJ_EXPECT(kj::str(256 * kj::MILLISECONDS) == "256ms");
  KJ_EXPECT(kj::str(5 * kj::MILLISECONDS + 2 * kj::NANOSECONDS) == "5.000002ms");
  KJ_EXPECT(kj::str(50 * kj::MICROSECONDS) == "50μs");
  KJ_EXPECT(kj::str(5 * kj::MICROSECONDS + 300 * kj::NANOSECONDS) == "5.3μs");
  KJ_EXPECT(kj::str(50 * kj::NANOSECONDS) == "50ns");
  KJ_EXPECT(kj::str(-256 * kj::MILLISECONDS) == "-256ms");
  KJ_EXPECT(kj::str(-50 * kj::NANOSECONDS) == "-50ns");
  KJ_EXPECT(kj::str((int64_t)kj::maxValue * kj::NANOSECONDS) == "9223372036.854775807s");
  KJ_EXPECT(kj::str((int64_t)kj::minValue * kj::NANOSECONDS) == "-9223372036.854775808s");
}

#if _WIN32
void delay(kj::Duration d) {
  Sleep(d / kj::MILLISECONDS);
}
#else
void delay(kj::Duration d) {
  usleep(d / kj::MICROSECONDS);
}
#endif

KJ_TEST("calendar clocks matches unix time") {
  // Check that the times returned by the calendar clock are within 1s of what time() returns.

  auto& coarse = systemCoarseCalendarClock();
  auto& precise = systemPreciseCalendarClock();

  Date p = precise.now();
  Date c = coarse.now();
  time_t t = time(nullptr);

  int64_t pi = (p - UNIX_EPOCH) / kj::SECONDS;
  int64_t ci = (c - UNIX_EPOCH) / kj::SECONDS;

  KJ_EXPECT(pi >= t - 1);
  KJ_EXPECT(pi <= t + 1);
  KJ_EXPECT(ci >= t - 1);
  KJ_EXPECT(ci <= t + 1);
}

KJ_TEST("monotonic clocks match each other") {
  // Check that the monotonic clocks return comparable times.

  auto& coarse = systemCoarseMonotonicClock();
  auto& precise = systemPreciseMonotonicClock();

  TimePoint p = precise.now();
  TimePoint c = coarse.now();

  // 40ms tolerance due to Windows timeslices being quite long, especially on GitHub Actions where
  // Windows is drunk and has completely lost track of time.
  KJ_EXPECT(p < c + 40 * kj::MILLISECONDS, p - c);
  KJ_EXPECT(p > c - 40 * kj::MILLISECONDS, c - p);
}

KJ_TEST("all clocks advance in real time") {
  Duration coarseCalDiff;
  Duration preciseCalDiff;
  Duration coarseMonoDiff;
  Duration preciseMonoDiff;

  for (uint retryCount KJ_UNUSED: kj::zeroTo(20)) {
    auto& coarseCal = systemCoarseCalendarClock();
    auto& preciseCal = systemPreciseCalendarClock();
    auto& coarseMono = systemCoarseMonotonicClock();
    auto& preciseMono = systemPreciseMonotonicClock();

    Date coarseCalBefore = coarseCal.now();
    Date preciseCalBefore = preciseCal.now();
    TimePoint coarseMonoBefore = coarseMono.now();
    TimePoint preciseMonoBefore = preciseMono.now();

    Duration delayTime = 150 * kj::MILLISECONDS;
    delay(delayTime);

    Date coarseCalAfter = coarseCal.now();
    Date preciseCalAfter = preciseCal.now();
    TimePoint coarseMonoAfter = coarseMono.now();
    TimePoint preciseMonoAfter = preciseMono.now();

    coarseCalDiff = coarseCalAfter - coarseCalBefore;
    preciseCalDiff = preciseCalAfter - preciseCalBefore;
    coarseMonoDiff = coarseMonoAfter - coarseMonoBefore;
    preciseMonoDiff = preciseMonoAfter - preciseMonoBefore;

    // 20ms tolerance due to Windows timeslices being quite long (and Windows sleeps being only
    // accurate to the timeslice).
    if (coarseCalDiff > delayTime - 20 * kj::MILLISECONDS &&
        coarseCalDiff < delayTime + 20 * kj::MILLISECONDS &&
        preciseCalDiff > delayTime - 20 * kj::MILLISECONDS &&
        preciseCalDiff < delayTime + 20 * kj::MILLISECONDS &&
        coarseMonoDiff > delayTime - 20 * kj::MILLISECONDS &&
        coarseMonoDiff < delayTime + 20 * kj::MILLISECONDS &&
        preciseMonoDiff > delayTime - 20 * kj::MILLISECONDS &&
        preciseMonoDiff < delayTime + 20 * kj::MILLISECONDS) {
      // success
      return;
    }
  }

  KJ_FAIL_EXPECT("clocks seem inaccurate even after 20 tries",
      coarseCalDiff / kj::MICROSECONDS, preciseCalDiff / kj::MICROSECONDS,
      coarseMonoDiff / kj::MICROSECONDS, preciseMonoDiff / kj::MICROSECONDS);
}

}  // namespace
}  // namespace kj
