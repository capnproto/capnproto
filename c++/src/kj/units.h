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

// This file contains types which are intended to help detect incorrect usage at compile
// time, but should then be optimized down to basic primitives (usually, integers) by the
// compiler.

#ifndef KJ_UNITS_H_
#define KJ_UNITS_H_

#include "common.h"

namespace kj {

// =======================================================================================
// IDs

template <typename UnderlyingType, typename Label>
struct Id {
  // A type-safe numeric ID.  `UnderlyingType` is the underlying integer representation.  `Label`
  // distinguishes this Id from other Id types.  Sample usage:
  //
  //   class Foo;
  //   typedef Id<uint, Foo> FooId;
  //
  //   class Bar;
  //   typedef Id<uint, Bar> BarId;
  //
  // You can now use the FooId and BarId types without any possibility of accidentally using a
  // FooId when you really wanted a BarId or vice-versa.

  UnderlyingType value;

  inline constexpr Id(): value(0) {}
  inline constexpr explicit Id(int value): value(value) {}

  inline constexpr bool operator==(const Id& other) const { return value == other.value; }
  inline constexpr bool operator!=(const Id& other) const { return value != other.value; }
  inline constexpr bool operator<=(const Id& other) const { return value <= other.value; }
  inline constexpr bool operator>=(const Id& other) const { return value >= other.value; }
  inline constexpr bool operator< (const Id& other) const { return value <  other.value; }
  inline constexpr bool operator> (const Id& other) const { return value >  other.value; }
};

// =======================================================================================
// Quantity and UnitRatio -- implement unit analysis via the type system

template <typename T> constexpr bool isIntegral() { return false; }
template <> constexpr bool isIntegral<char>() { return true; }
template <> constexpr bool isIntegral<signed char>() { return true; }
template <> constexpr bool isIntegral<short>() { return true; }
template <> constexpr bool isIntegral<int>() { return true; }
template <> constexpr bool isIntegral<long>() { return true; }
template <> constexpr bool isIntegral<long long>() { return true; }
template <> constexpr bool isIntegral<unsigned char>() { return true; }
template <> constexpr bool isIntegral<unsigned short>() { return true; }
template <> constexpr bool isIntegral<unsigned int>() { return true; }
template <> constexpr bool isIntegral<unsigned long>() { return true; }
template <> constexpr bool isIntegral<unsigned long long>() { return true; }

template <typename Number, typename Unit1, typename Unit2>
class UnitRatio {
  // A multiplier used to convert Quantities of one unit to Quantities of another unit.  See
  // Quantity, below.
  //
  // Construct this type by dividing one Quantity by another of a different unit.  Use this type
  // by multiplying it by a Quantity, or dividing a Quantity by it.

  static_assert(isIntegral<Number>(), "Underlying type for UnitRatio must be integer.");

public:
  inline UnitRatio() {}

  constexpr explicit UnitRatio(Number unit1PerUnit2): unit1PerUnit2(unit1PerUnit2) {}
  // This constructor was intended to be private, but GCC complains about it being private in a
  // bunch of places that don't appear to even call it, so I made it public.  Oh well.

  template <typename OtherNumber>
  inline constexpr UnitRatio(const UnitRatio<OtherNumber, Unit1, Unit2>& other)
      : unit1PerUnit2(other.unit1PerUnit2) {}

  template <typename OtherNumber>
  inline constexpr UnitRatio<decltype(Number(1)+OtherNumber(1)), Unit1, Unit2>
      operator+(UnitRatio<OtherNumber, Unit1, Unit2> other) const {
    return UnitRatio<decltype(Number(1)+OtherNumber(1)), Unit1, Unit2>(
        unit1PerUnit2 + other.unit1PerUnit2);
  }
  template <typename OtherNumber>
  inline constexpr UnitRatio<decltype(Number(1)-OtherNumber(1)), Unit1, Unit2>
      operator-(UnitRatio<OtherNumber, Unit1, Unit2> other) const {
    return UnitRatio<decltype(Number(1)-OtherNumber(1)), Unit1, Unit2>(
        unit1PerUnit2 - other.unit1PerUnit2);
  }

  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>
      operator*(UnitRatio<OtherNumber, Unit3, Unit1> other) const {
    // U1 / U2 * U3 / U1 = U3 / U2
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>(
        unit1PerUnit2 * other.unit1PerUnit2);
  }
  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>
      operator*(UnitRatio<OtherNumber, Unit2, Unit3> other) const {
    // U1 / U2 * U2 / U3 = U1 / U3
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>(
        unit1PerUnit2 * other.unit1PerUnit2);
  }

  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>
      operator/(UnitRatio<OtherNumber, Unit1, Unit3> other) const {
    // (U1 / U2) / (U1 / U3) = U3 / U2
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>(
        unit1PerUnit2 / other.unit1PerUnit2);
  }
  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>
      operator/(UnitRatio<OtherNumber, Unit3, Unit2> other) const {
    // (U1 / U2) / (U3 / U2) = U1 / U3
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>(
        unit1PerUnit2 / other.unit1PerUnit2);
  }

  template <typename OtherNumber>
  inline decltype(Number(1) / OtherNumber(1))
      operator/(UnitRatio<OtherNumber, Unit1, Unit2> other) const {
    return unit1PerUnit2 / other.unit1PerUnit2;
  }

  inline bool operator==(UnitRatio other) const { return unit1PerUnit2 == other.unit1PerUnit2; }
  inline bool operator!=(UnitRatio other) const { return unit1PerUnit2 != other.unit1PerUnit2; }

private:
  Number unit1PerUnit2;

  template <typename OtherNumber, typename OtherUnit>
  friend class Quantity;
  template <typename OtherNumber, typename OtherUnit1, typename OtherUnit2>
  friend class UnitRatio;

  template <typename N1, typename N2, typename U1, typename U2>
  friend inline constexpr UnitRatio<decltype(N1(1) * N2(1)), U1, U2>
      operator*(N1, UnitRatio<N2, U1, U2>);
};

template <typename N1, typename N2, typename U1, typename U2>
inline constexpr UnitRatio<decltype(N1(1) * N2(1)), U1, U2>
    operator*(N1 n, UnitRatio<N2, U1, U2> r) {
  return UnitRatio<decltype(N1(1) * N2(1)), U1, U2>(n * r.unit1PerUnit2);
}

template <typename Number, typename Unit>
class Quantity {
  // A type-safe numeric quantity, specified in terms of some unit.  Two Quantities cannot be used
  // in arithmetic unless they use the same unit.  The `Unit` type parameter is only used to prevent
  // accidental mixing of units; this type is never instantiated and can very well be incomplete.
  // `Number` is the underlying primitive numeric type.
  //
  // Quantities support most basic arithmetic operators, intelligently handling units, and
  // automatically casting the underlying type in the same way that the compiler would.
  //
  // To convert a primitive number to a Quantity, multiply it by unit<Quantity<N, U>>().
  // To convert a Quantity to a primitive number, divide it by unit<Quantity<N, U>>().
  // To convert a Quantity of one unit to another unit, multiply or divide by a UnitRatio.
  //
  // The Quantity class is not well-suited to hardcore physics as it does not allow multiplying
  // one quantity by another.  For example, multiplying meters by meters won't get you square
  // meters; it will get you a compiler error.  It would be interesting to see if template
  // metaprogramming could properly deal with such things but this isn't needed for the present
  // use case.
  //
  // Sample usage:
  //
  //   class SecondsLabel;
  //   typedef Quantity<double, SecondsLabel> Seconds;
  //   constexpr Seconds SECONDS = unit<Seconds>();
  //
  //   class MinutesLabel;
  //   typedef Quantity<double, MinutesLabel> Minutes;
  //   constexpr Minutes MINUTES = unit<Minutes>();
  //
  //   constexpr UnitRatio<double, SecondsLabel, MinutesLabel> SECONDS_PER_MINUTE =
  //       60 * SECONDS / MINUTES;
  //
  //   void waitFor(Seconds seconds) {
  //     sleep(seconds / SECONDS);
  //   }
  //   void waitFor(Minutes minutes) {
  //     waitFor(minutes * SECONDS_PER_MINUTE);
  //   }
  //
  //   void waitThreeMinutes() {
  //     waitFor(3 * MINUTES);
  //   }

  static_assert(isIntegral<Number>(), "Underlying type for Quantity must be integer.");

public:
  inline constexpr Quantity() {}

  inline constexpr Quantity(decltype(maxValue)): value(maxValue) {}
  inline constexpr Quantity(decltype(minValue)): value(minValue) {}
  // Allow initialization from maxValue and minValue.

  inline explicit constexpr Quantity(Number value): value(value) {}
  // This constructor was intended to be private, but GCC complains about it being private in a
  // bunch of places that don't appear to even call it, so I made it public.  Oh well.

  template <typename OtherNumber>
  inline constexpr Quantity(const Quantity<OtherNumber, Unit>& other)
      : value(other.value) {}

  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) + OtherNumber(1)), Unit>
      operator+(const Quantity<OtherNumber, Unit>& other) const {
    return Quantity<decltype(Number(1) + OtherNumber(1)), Unit>(value + other.value);
  }
  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) - OtherNumber(1)), Unit>
      operator-(const Quantity<OtherNumber, Unit>& other) const {
    return Quantity<decltype(Number(1) - OtherNumber(1)), Unit>(value - other.value);
  }
  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) * OtherNumber(1)), Unit>
      operator*(OtherNumber other) const {
    static_assert(isIntegral<OtherNumber>(), "Multiplied Quantity by non-integer.");
    return Quantity<decltype(Number(1) * other), Unit>(value * other);
  }
  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) / OtherNumber(1)), Unit>
      operator/(OtherNumber other) const {
    static_assert(isIntegral<OtherNumber>(), "Divided Quantity by non-integer.");
    return Quantity<decltype(Number(1) / other), Unit>(value / other);
  }
  template <typename OtherNumber>
  inline constexpr decltype(Number(1) / OtherNumber(1))
      operator/(const Quantity<OtherNumber, Unit>& other) const {
    return value / other.value;
  }
  template <typename OtherNumber>
  inline constexpr decltype(Number(1) % OtherNumber(1))
      operator%(const Quantity<OtherNumber, Unit>& other) const {
    return value % other.value;
  }

  template <typename OtherNumber, typename OtherUnit>
  inline constexpr Quantity<decltype(Number(1) * OtherNumber(1)), OtherUnit>
      operator*(const UnitRatio<OtherNumber, OtherUnit, Unit>& ratio) const {
    return Quantity<decltype(Number(1) * OtherNumber(1)), OtherUnit>(
        value * ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr Quantity<decltype(Number(1) / OtherNumber(1)), OtherUnit>
      operator/(const UnitRatio<OtherNumber, Unit, OtherUnit>& ratio) const {
    return Quantity<decltype(Number(1) / OtherNumber(1)), OtherUnit>(
        value / ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr Quantity<decltype(Number(1) % OtherNumber(1)), Unit>
      operator%(const UnitRatio<OtherNumber, Unit, OtherUnit>& ratio) const {
    return Quantity<decltype(Number(1) % OtherNumber(1)), Unit>(
        value % ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr UnitRatio<decltype(Number(1) / OtherNumber(1)), Unit, OtherUnit>
      operator/(const Quantity<OtherNumber, OtherUnit>& other) const {
    return UnitRatio<decltype(Number(1) / OtherNumber(1)), Unit, OtherUnit>(value / other.value);
  }

  template <typename OtherNumber>
  inline constexpr bool operator==(const Quantity<OtherNumber, Unit>& other) const {
    return value == other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator!=(const Quantity<OtherNumber, Unit>& other) const {
    return value != other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator<=(const Quantity<OtherNumber, Unit>& other) const {
    return value <= other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator>=(const Quantity<OtherNumber, Unit>& other) const {
    return value >= other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator<(const Quantity<OtherNumber, Unit>& other) const {
    return value < other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator>(const Quantity<OtherNumber, Unit>& other) const {
    return value > other.value;
  }

  template <typename OtherNumber>
  inline Quantity& operator+=(const Quantity<OtherNumber, Unit>& other) {
    value += other.value;
    return *this;
  }
  template <typename OtherNumber>
  inline Quantity& operator-=(const Quantity<OtherNumber, Unit>& other) {
    value -= other.value;
    return *this;
  }
  template <typename OtherNumber>
  inline Quantity& operator*=(OtherNumber other) {
    value *= other;
    return *this;
  }
  template <typename OtherNumber>
  inline Quantity& operator/=(OtherNumber other) {
    value /= other.value;
    return *this;
  }

private:
  Number value;

  template <typename OtherNumber, typename OtherUnit>
  friend class Quantity;

  template <typename Number1, typename Number2, typename Unit2>
  friend inline constexpr auto operator*(Number1 a, Quantity<Number2, Unit2> b)
      -> Quantity<decltype(Number1(1) * Number2(1)), Unit2>;

  template <typename T>
  friend inline constexpr T unit();
};

template <typename T>
inline constexpr T unit() { return T(1); }
// unit<Quantity<T, U>>() returns a Quantity of value 1.  It also, intentionally, works on basic
// numeric types.

template <typename Number1, typename Number2, typename Unit>
inline constexpr auto operator*(Number1 a, Quantity<Number2, Unit> b)
    -> Quantity<decltype(Number1(1) * Number2(1)), Unit> {
  return Quantity<decltype(Number1(1) * Number2(1)), Unit>(a * b.value);
}

template <typename Number1, typename Number2, typename Unit, typename Unit2>
inline constexpr auto operator*(UnitRatio<Number1, Unit2, Unit> ratio,
    Quantity<Number2, Unit> measure)
    -> decltype(measure * ratio) {
  return measure * ratio;
}

// =======================================================================================
// Absolute measures

template <typename T, typename Label>
class Absolute {
  // Wraps some other value -- typically a Quantity -- but represents a value measured based on
  // some absolute origin.  For exmaple, if `Duration` is a type representing a time duration,
  // Absolute<Duration, UnixEpoch> might be a calendar date.
  //
  // Since Absolute represents measurements relative to some arbitrary origin, the only sensible
  // arithmetic to perform on them is addition and subtraction.

  // TODO(someday):  Do the same automatic expansion of integer width that Quantity does?  Doesn't
  //   matter for our time use case, where we always use 64-bit anyway.  Note that fixing this
  //   would implicitly allow things like multiplying an Absolute by a UnitRatio to change its
  //   units, which is actually totally logical and kind of neat.

public:
  inline constexpr Absolute operator+(const T& other) const { return Absolute(value + other); }
  inline constexpr Absolute operator-(const T& other) const { return Absolute(value - other); }
  inline constexpr T operator-(const Absolute& other) const { return value - other.value; }

  inline Absolute& operator+=(const T& other) { value += other; return *this; }
  inline Absolute& operator-=(const T& other) { value -= other; return *this; }

  inline constexpr bool operator==(const Absolute& other) const { return value == other.value; }
  inline constexpr bool operator!=(const Absolute& other) const { return value != other.value; }
  inline constexpr bool operator<=(const Absolute& other) const { return value <= other.value; }
  inline constexpr bool operator>=(const Absolute& other) const { return value >= other.value; }
  inline constexpr bool operator< (const Absolute& other) const { return value <  other.value; }
  inline constexpr bool operator> (const Absolute& other) const { return value >  other.value; }

private:
  T value;

  explicit constexpr Absolute(T value): value(value) {}

  template <typename U>
  friend inline constexpr U origin();
};

template <typename T, typename Label>
inline constexpr Absolute<T, Label> operator+(const T& a, const Absolute<T, Label>& b) {
  return b + a;
}

template <typename T> struct UnitOf_ { typedef T Type; };
template <typename T, typename Label> struct UnitOf_<Absolute<T, Label>> { typedef T Type; };
template <typename T>
using UnitOf = typename UnitOf_<T>::Type;
// UnitOf<Absolute<T, U>> is T.  UnitOf<AnythingElse> is AnythingElse.

template <typename T>
inline constexpr T origin() { return T(0 * unit<UnitOf<T>>()); }
// origin<Absolute<T, U>>() returns an Absolute of value 0.  It also, intentionally, works on basic
// numeric types.

}  // namespace kj

#endif  // KJ_UNITS_H_
