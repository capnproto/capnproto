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

#if _WIN32
#define WIN32_LEAN_AND_MEAN 1  // lolz
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#endif

#include "time.h"
#include "debug.h"
#include <set>

#if _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

namespace kj {

const Clock& nullClock() {
  class NullClock final: public Clock {
  public:
    Date now() const override { return UNIX_EPOCH; }
  };
  static KJ_CONSTEXPR(const) NullClock NULL_CLOCK = NullClock();
  return NULL_CLOCK;
}

#if _WIN32

namespace {

static constexpr int64_t WIN32_EPOCH_OFFSET = 116444736000000000ull;
// Number of 100ns intervals from Jan 1, 1601 to Jan 1, 1970.

static Date toKjDate(FILETIME t) {
  int64_t value = (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  return (value - WIN32_EPOCH_OFFSET) * (100 * kj::NANOSECONDS) + UNIX_EPOCH;
}

class Win32CoarseClock: public Clock {
public:
  Date now() const override {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return toKjDate(ft);
  }
};

class Win32PreciseClock: public Clock {
  typedef VOID WINAPI GetSystemTimePreciseAsFileTimeFunc(LPFILETIME);
public:
  Date now() const override {
    static const GetSystemTimePreciseAsFileTimeFunc* getSystemTimePreciseAsFileTimePtr =
        getGetSystemTimePreciseAsFileTime();
    FILETIME ft;
    if (getSystemTimePreciseAsFileTimePtr == nullptr) {
      // We can't use QueryPerformanceCounter() to get any more precision because we have no way
      // of knowing when the calendar clock jumps. So I guess we're stuck.
      GetSystemTimeAsFileTime(&ft);
    } else {
      getSystemTimePreciseAsFileTimePtr(&ft);
    }
    return toKjDate(ft);
  }

private:
  static GetSystemTimePreciseAsFileTimeFunc* getGetSystemTimePreciseAsFileTime() {
    // Dynamically look up the function GetSystemTimePreciseAsFileTimeFunc(). This was only
    // introduced as of Windows 8, so it might be missing.
    return reinterpret_cast<GetSystemTimePreciseAsFileTimeFunc*>(GetProcAddress(
      GetModuleHandleA("kernel32.dll"),
      "GetSystemTimePreciseAsFileTime"));
  }
};

class Win32CoarseMonotonicClock: public MonotonicClock {
public:
  TimePoint now() const override {
    return kj::origin<TimePoint>() + GetTickCount64() * kj::MILLISECONDS;
  }
};

class Win32PreciseMonotonicClock: public MonotonicClock {
  // Precise clock implemented using QueryPerformanceCounter().
  //
  // TODO(someday): Windows 10 has QueryUnbiasedInterruptTime() and
  //   QueryUnbiasedInterruptTimePrecise(), a new API for monotonic timing that isn't as difficult.
  //   Is there any benefit to dynamically checking for these and using them if available?

public:
  TimePoint now() const override {
    static const QpcProperties props;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t adjusted = now.QuadPart - props.origin;
    uint64_t ns = mulDiv64(adjusted, 1'000'000'000, props.frequency);
    return kj::origin<TimePoint>() + ns * kj::NANOSECONDS;
  }

private:
  struct QpcProperties {
    uint64_t origin;
    // What QueryPerformanceCounter() would have returned at the time when GetTickCount64() returned
    // zero. Used to ensure that the coarse and precise timers return similar values.

    uint64_t frequency;
    // From QueryPerformanceFrequency().

    QpcProperties() {
      LARGE_INTEGER now, freqLi;
      uint64_t ticks = GetTickCount64();
      QueryPerformanceCounter(&now);

      QueryPerformanceFrequency(&freqLi);
      frequency = freqLi.QuadPart;

      // Convert the millisecond tick count into performance counter ticks.
      uint64_t ticksAsQpc = mulDiv64(ticks, freqLi.QuadPart, 1000);

      origin = now.QuadPart - ticksAsQpc;
    }
  };

  static inline uint64_t mulDiv64(uint64_t value, uint64_t numer, uint64_t denom) {
    // Inspired by:
    //   https://github.com/rust-lang/rust/pull/22788/files#diff-24f054cd23f65af3b574c6ce8aa5a837R54
    // Computes (value*numer)/denom without overflow, as long as both
    // (numer*denom) and the overall result fit into 64 bits.
    uint64_t q = value / denom;
    uint64_t r = value % denom;
    return q * numer + r * numer / denom;
  }
};

}  // namespace

const Clock& systemCoarseCalendarClock() {
  static constexpr Win32CoarseClock clock;
  return clock;
}
const Clock& systemPreciseCalendarClock() {
  static constexpr Win32PreciseClock clock;
  return clock;
}

const MonotonicClock& systemCoarseMonotonicClock() {
  static constexpr Win32CoarseMonotonicClock clock;
  return clock;
}
const MonotonicClock& systemPreciseMonotonicClock() {
  static constexpr Win32PreciseMonotonicClock clock;
  return clock;
}

#else

namespace {

class PosixClock: public Clock {
public:
  constexpr PosixClock(clockid_t clockId): clockId(clockId) {}

  Date now() const override {
    struct timespec ts;
    KJ_SYSCALL(clock_gettime(clockId, &ts));
    return UNIX_EPOCH + ts.tv_sec * kj::SECONDS + ts.tv_nsec * kj::NANOSECONDS;
  }

private:
  clockid_t clockId;
};

class PosixMonotonicClock: public MonotonicClock {
public:
  constexpr PosixMonotonicClock(clockid_t clockId): clockId(clockId) {}

  TimePoint now() const override {
    struct timespec ts;
    KJ_SYSCALL(clock_gettime(clockId, &ts));
    return kj::origin<TimePoint>() + ts.tv_sec * kj::SECONDS + ts.tv_nsec * kj::NANOSECONDS;
  }

private:
  clockid_t clockId;
};

}  // namespace

// FreeBSD has "_PRECISE", but Linux just defaults to precise.
#ifndef CLOCK_REALTIME_PRECISE
#define CLOCK_REALTIME_PRECISE CLOCK_REALTIME
#endif

#ifndef CLOCK_MONOTONIC_PRECISE
#define CLOCK_MONOTONIC_PRECISE CLOCK_MONOTONIC
#endif

// FreeBSD has "_FAST", Linux has "_COARSE".
// MacOS has an "_APPROX" but only for CLOCK_MONOTONIC_RAW, which isn't helpful.
#ifndef CLOCK_REALTIME_COARSE
#ifdef CLOCK_REALTIME_FAST
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME_FAST
#else
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#endif
#endif

#ifndef CLOCK_MONOTONIC_COARSE
#ifdef CLOCK_MONOTONIC_FAST
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC_FAST
#else
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC
#endif
#endif

const Clock& systemCoarseCalendarClock() {
  static constexpr PosixClock clock(CLOCK_REALTIME_COARSE);
  return clock;
}
const Clock& systemPreciseCalendarClock() {
  static constexpr PosixClock clock(CLOCK_REALTIME_PRECISE);
  return clock;
}

const MonotonicClock& systemCoarseMonotonicClock() {
  static constexpr PosixMonotonicClock clock(CLOCK_MONOTONIC_COARSE);
  return clock;
}
const MonotonicClock& systemPreciseMonotonicClock() {
  static constexpr PosixMonotonicClock clock(CLOCK_MONOTONIC_PRECISE);
  return clock;
}

#endif

}  // namespace kj
