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

#include "string.h"
#include "debug.h"
#include <stdio.h>
#include <float.h>
#include <errno.h>
#include <stdlib.h>

namespace kj {

#if _MSC_VER
#pragma warning(disable: 4996)
// Warns that sprintf() is buffer-overrunny. We know that, it's cool.
#endif

String heapString(size_t size) {
  char* buffer = _::HeapArrayDisposer::allocate<char>(size + 1);
  buffer[size] = '\0';
  return String(buffer, size, _::HeapArrayDisposer::instance);
}

String heapString(const char* value, size_t size) {
  char* buffer = _::HeapArrayDisposer::allocate<char>(size + 1);
  memcpy(buffer, value, size);
  buffer[size] = '\0';
  return String(buffer, size, _::HeapArrayDisposer::instance);
}

#define HEXIFY_INT(type, format) \
CappedArray<char, sizeof(type) * 2 + 1> hex(type i) { \
  CappedArray<char, sizeof(type) * 2 + 1> result; \
  result.setSize(sprintf(result.begin(), format, i)); \
  return result; \
}

HEXIFY_INT(unsigned char, "%x");
HEXIFY_INT(unsigned short, "%x");
HEXIFY_INT(unsigned int, "%x");
HEXIFY_INT(unsigned long, "%lx");
HEXIFY_INT(unsigned long long, "%llx");

#undef HEXIFY_INT

namespace _ {  // private

StringPtr Stringifier::operator*(decltype(nullptr)) const {
  return "nullptr";
}

StringPtr Stringifier::operator*(bool b) const {
  return b ? StringPtr("true") : StringPtr("false");
}

#define STRINGIFY_INT(type, format) \
CappedArray<char, sizeof(type) * 3 + 2> Stringifier::operator*(type i) const { \
  CappedArray<char, sizeof(type) * 3 + 2> result; \
  result.setSize(sprintf(result.begin(), format, i)); \
  return result; \
}

STRINGIFY_INT(signed char, "%d");
STRINGIFY_INT(unsigned char, "%u");
STRINGIFY_INT(short, "%d");
STRINGIFY_INT(unsigned short, "%u");
STRINGIFY_INT(int, "%d");
STRINGIFY_INT(unsigned int, "%u");
STRINGIFY_INT(long, "%ld");
STRINGIFY_INT(unsigned long, "%lu");
STRINGIFY_INT(long long, "%lld");
STRINGIFY_INT(unsigned long long, "%llu");
STRINGIFY_INT(const void*, "%p");

#undef STRINGIFY_INT

namespace {

// ----------------------------------------------------------------------
// DoubleToBuffer()
// FloatToBuffer()
//    Copied from Protocol Buffers, (C) Google, BSD license.
//    Kenton wrote this code originally.  The following commentary is
//    from the original.
//
//    Description: converts a double or float to a string which, if
//    passed to NoLocaleStrtod(), will produce the exact same original double
//    (except in case of NaN; all NaNs are considered the same value).
//    We try to keep the string short but it's not guaranteed to be as
//    short as possible.
//
//    DoubleToBuffer() and FloatToBuffer() write the text to the given
//    buffer and return it.  The buffer must be at least
//    kDoubleToBufferSize bytes for doubles and kFloatToBufferSize
//    bytes for floats.  kFastToBufferSize is also guaranteed to be large
//    enough to hold either.
//
//    We want to print the value without losing precision, but we also do
//    not want to print more digits than necessary.  This turns out to be
//    trickier than it sounds.  Numbers like 0.2 cannot be represented
//    exactly in binary.  If we print 0.2 with a very large precision,
//    e.g. "%.50g", we get "0.2000000000000000111022302462515654042363167".
//    On the other hand, if we set the precision too low, we lose
//    significant digits when printing numbers that actually need them.
//    It turns out there is no precision value that does the right thing
//    for all numbers.
//
//    Our strategy is to first try printing with a precision that is never
//    over-precise, then parse the result with strtod() to see if it
//    matches.  If not, we print again with a precision that will always
//    give a precise result, but may use more digits than necessary.
//
//    An arguably better strategy would be to use the algorithm described
//    in "How to Print Floating-Point Numbers Accurately" by Steele &
//    White, e.g. as implemented by David M. Gay's dtoa().  It turns out,
//    however, that the following implementation is about as fast as
//    DMG's code.  Furthermore, DMG's code locks mutexes, which means it
//    will not scale well on multi-core machines.  DMG's code is slightly
//    more accurate (in that it will never use more digits than
//    necessary), but this is probably irrelevant for most users.
//
//    Rob Pike and Ken Thompson also have an implementation of dtoa() in
//    third_party/fmt/fltfmt.cc.  Their implementation is similar to this
//    one in that it makes guesses and then uses strtod() to check them.
//    Their implementation is faster because they use their own code to
//    generate the digits in the first place rather than use snprintf(),
//    thus avoiding format string parsing overhead.  However, this makes
//    it considerably more complicated than the following implementation,
//    and it is embedded in a larger library.  If speed turns out to be
//    an issue, we could re-implement this in terms of their
//    implementation.
// ----------------------------------------------------------------------

#ifdef _WIN32
// MSVC has only _snprintf, not snprintf.
//
// MinGW has both snprintf and _snprintf, but they appear to be different
// functions.  The former is buggy.  When invoked like so:
//   char buffer[32];
//   snprintf(buffer, 32, "%.*g\n", FLT_DIG, 1.23e10f);
// it prints "1.23000e+10".  This is plainly wrong:  %g should never print
// trailing zeros after the decimal point.  For some reason this bug only
// occurs with some input values, not all.  In any case, _snprintf does the
// right thing, so we use it.
#define snprintf _snprintf
#endif

inline bool IsNaN(double value) {
  // NaN is never equal to anything, even itself.
  return value != value;
}

// In practice, doubles should never need more than 24 bytes and floats
// should never need more than 14 (including null terminators), but we
// overestimate to be safe.
static const int kDoubleToBufferSize = 32;
static const int kFloatToBufferSize = 24;

static inline bool IsValidFloatChar(char c) {
  return ('0' <= c && c <= '9') ||
         c == 'e' || c == 'E' ||
         c == '+' || c == '-';
}

void DelocalizeRadix(char* buffer) {
  // Fast check:  if the buffer has a normal decimal point, assume no
  // translation is needed.
  if (strchr(buffer, '.') != NULL) return;

  // Find the first unknown character.
  while (IsValidFloatChar(*buffer)) ++buffer;

  if (*buffer == '\0') {
    // No radix character found.
    return;
  }

  // We are now pointing at the locale-specific radix character.  Replace it
  // with '.'.
  *buffer = '.';
  ++buffer;

  if (!IsValidFloatChar(*buffer) && *buffer != '\0') {
    // It appears the radix was a multi-byte character.  We need to remove the
    // extra bytes.
    char* target = buffer;
    do { ++buffer; } while (!IsValidFloatChar(*buffer) && *buffer != '\0');
    memmove(target, buffer, strlen(buffer) + 1);
  }
}

void RemovePlus(char* buffer) {
  // Remove any + characters because they are redundant and ugly.

  for (;;) {
    buffer = strchr(buffer, '+');
    if (buffer == NULL) {
      return;
    }
    memmove(buffer, buffer + 1, strlen(buffer + 1) + 1);
  }
}

#if _WIN32
void RemoveE0(char* buffer) {
  // Remove redundant leading 0's after an e, e.g. 1e012. Seems to appear on
  // Windows.

  for (;;) {
    buffer = strstr(buffer, "e0");
    if (buffer == NULL || buffer[2] < '0' || buffer[2] > '9') {
      return;
    }
    memmove(buffer + 1, buffer + 2, strlen(buffer + 2) + 1);
  }
}
#endif

char* DoubleToBuffer(double value, char* buffer) {
  // DBL_DIG is 15 for IEEE-754 doubles, which are used on almost all
  // platforms these days.  Just in case some system exists where DBL_DIG
  // is significantly larger -- and risks overflowing our buffer -- we have
  // this assert.
  static_assert(DBL_DIG < 20, "DBL_DIG is too big.");

  if (value == inf()) {
    strcpy(buffer, "inf");
    return buffer;
  } else if (value == -inf()) {
    strcpy(buffer, "-inf");
    return buffer;
  } else if (IsNaN(value)) {
    strcpy(buffer, "nan");
    return buffer;
  }

  int snprintf_result KJ_UNUSED =
    snprintf(buffer, kDoubleToBufferSize, "%.*g", DBL_DIG, value);

  // The snprintf should never overflow because the buffer is significantly
  // larger than the precision we asked for.
  KJ_DASSERT(snprintf_result > 0 && snprintf_result < kDoubleToBufferSize);

  // We need to make parsed_value volatile in order to force the compiler to
  // write it out to the stack.  Otherwise, it may keep the value in a
  // register, and if it does that, it may keep it as a long double instead
  // of a double.  This long double may have extra bits that make it compare
  // unequal to "value" even though it would be exactly equal if it were
  // truncated to a double.
  volatile double parsed_value = strtod(buffer, NULL);
  if (parsed_value != value) {
    int snprintf_result2 KJ_UNUSED =
      snprintf(buffer, kDoubleToBufferSize, "%.*g", DBL_DIG+2, value);

    // Should never overflow; see above.
    KJ_DASSERT(snprintf_result2 > 0 && snprintf_result2 < kDoubleToBufferSize);
  }

  DelocalizeRadix(buffer);
  RemovePlus(buffer);
#if _WIN32
  RemoveE0(buffer);
#endif // _WIN32
  return buffer;
}

bool safe_strtof(const char* str, float* value) {
  char* endptr;
  errno = 0;  // errno only gets set on errors
#if defined(_WIN32) || defined (__hpux)  // has no strtof()
  *value = static_cast<float>(strtod(str, &endptr));
#else
  *value = strtof(str, &endptr);
#endif
  return *str != 0 && *endptr == 0 && errno == 0;
}

char* FloatToBuffer(float value, char* buffer) {
  // FLT_DIG is 6 for IEEE-754 floats, which are used on almost all
  // platforms these days.  Just in case some system exists where FLT_DIG
  // is significantly larger -- and risks overflowing our buffer -- we have
  // this assert.
  static_assert(FLT_DIG < 10, "FLT_DIG is too big");

  if (value == inf()) {
    strcpy(buffer, "inf");
    return buffer;
  } else if (value == -inf()) {
    strcpy(buffer, "-inf");
    return buffer;
  } else if (IsNaN(value)) {
    strcpy(buffer, "nan");
    return buffer;
  }

  int snprintf_result KJ_UNUSED =
    snprintf(buffer, kFloatToBufferSize, "%.*g", FLT_DIG, value);

  // The snprintf should never overflow because the buffer is significantly
  // larger than the precision we asked for.
  KJ_DASSERT(snprintf_result > 0 && snprintf_result < kFloatToBufferSize);

  float parsed_value;
  if (!safe_strtof(buffer, &parsed_value) || parsed_value != value) {
    int snprintf_result2 KJ_UNUSED =
      snprintf(buffer, kFloatToBufferSize, "%.*g", FLT_DIG+2, value);

    // Should never overflow; see above.
    KJ_DASSERT(snprintf_result2 > 0 && snprintf_result2 < kFloatToBufferSize);
  }

  DelocalizeRadix(buffer);
  RemovePlus(buffer);
  return buffer;
}

}  // namespace

CappedArray<char, kFloatToBufferSize> Stringifier::operator*(float f) const {
  CappedArray<char, kFloatToBufferSize> result;
  result.setSize(strlen(FloatToBuffer(f, result.begin())));
  return result;
}

CappedArray<char, kDoubleToBufferSize> Stringifier::operator*(double f) const {
  CappedArray<char, kDoubleToBufferSize> result;
  result.setSize(strlen(DoubleToBuffer(f, result.begin())));
  return result;
}

}  // namespace _ (private)
}  // namespace kj
