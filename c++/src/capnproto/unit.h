// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
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

#ifndef CAPNPROTO_UNIT_H_
#define CAPNPROTO_UNIT_H_

namespace capnproto {

template <typename Number, typename Unit1, typename Unit2>
class UnitRatio {
public:
  constexpr UnitRatio(Number unit1PerUnit2): unit1PerUnit2(unit1PerUnit2) {}

  Number unit1PerUnit2;
};

template <typename T>
T any();

template <typename Number, typename Unit>
class UnitMeasure {
public:
  inline constexpr UnitMeasure() {}

  template <typename OtherNumber>
  inline constexpr UnitMeasure(const UnitMeasure<OtherNumber, Unit>& other)
      : value(other.value) {}

  static constexpr UnitMeasure ONE = UnitMeasure(1);

  template <typename OtherNumber>
  inline constexpr auto operator+(const UnitMeasure<OtherNumber, Unit>& other) const
      -> UnitMeasure<decltype(any<Number>() + any<OtherNumber>()), Unit> {
    return UnitMeasure<decltype(any<Number>() + any<OtherNumber>()), Unit>(value + other.value);
  }
  template <typename OtherNumber>
  inline constexpr auto operator-(const UnitMeasure<OtherNumber, Unit>& other) const
      -> UnitMeasure<decltype(any<Number>() - any<OtherNumber>()), Unit> {
    return UnitMeasure<decltype(any<Number>() - any<OtherNumber>()), Unit>(value - other.value);
  }
  template <typename OtherNumber>
  inline constexpr auto operator*(OtherNumber other) const
      -> UnitMeasure<decltype(any<Number>() * other), Unit> {
    return UnitMeasure<decltype(any<Number>() * other), Unit>(value * other);
  }
  template <typename OtherNumber>
  inline constexpr auto operator/(OtherNumber other) const
      -> UnitMeasure<decltype(any<Number>() / other), Unit> {
    return UnitMeasure<decltype(any<Number>() / other), Unit>(value / other);
  }
  template <typename OtherNumber>
  inline constexpr auto operator/(const UnitMeasure<OtherNumber, Unit>& other) const
      -> decltype(any<Number>() / any<OtherNumber>()) {
    return value / other.value;
  }
  template <typename OtherNumber>
  inline constexpr auto operator%(const UnitMeasure<OtherNumber, Unit>& other) const
      -> decltype(any<Number>() % any<OtherNumber>()) {
    return value % other.value;
  }

  template <typename OtherNumber, typename OtherUnit>
  inline constexpr auto operator*(const UnitRatio<OtherNumber, OtherUnit, Unit>& ratio) const
      -> UnitMeasure<decltype(any<Number>() * any<OtherNumber>()), OtherUnit> {
    return UnitMeasure<decltype(any<Number>() * any<OtherNumber>()), OtherUnit>(
        value * ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr auto operator/(const UnitRatio<OtherNumber, Unit, OtherUnit>& ratio) const
      -> UnitMeasure<decltype(any<Number>() / any<OtherNumber>()), OtherUnit> {
    return UnitMeasure<decltype(any<Number>() / any<OtherNumber>()), OtherUnit>(
        value / ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr auto operator%(const UnitRatio<OtherNumber, Unit, OtherUnit>& ratio) const
      -> UnitMeasure<decltype(any<Number>() % any<OtherNumber>()), Unit> {
    return UnitMeasure<decltype(any<Number>() % any<OtherNumber>()), Unit>(
        value % ratio.unit1PerUnit2);
  }

  template <typename OtherNumber>
  inline constexpr bool operator==(const UnitMeasure<OtherNumber, Unit>& other) const {
    return value == other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator!=(const UnitMeasure<OtherNumber, Unit>& other) const {
    return value != other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator<=(const UnitMeasure<OtherNumber, Unit>& other) const {
    return value <= other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator>=(const UnitMeasure<OtherNumber, Unit>& other) const {
    return value >= other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator<(const UnitMeasure<OtherNumber, Unit>& other) const {
    return value < other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator>(const UnitMeasure<OtherNumber, Unit>& other) const {
    return value > other.value;
  }

  template <typename OtherNumber>
  inline UnitMeasure& operator+=(const UnitMeasure<OtherNumber, Unit>& other) {
    value += other.value;
    return *this;
  }
  template <typename OtherNumber>
  inline UnitMeasure& operator-=(const UnitMeasure<OtherNumber, Unit>& other) {
    value -= other.value;
    return *this;
  }
  template <typename OtherNumber>
  inline UnitMeasure& operator*=(OtherNumber other) {
    value *= other;
    return *this;
  }
  template <typename OtherNumber>
  inline UnitMeasure& operator/=(OtherNumber other) {
    value /= other.value;
    return *this;
  }

private:
  inline explicit constexpr UnitMeasure(Number value): value(value) {}

  Number value;

  template <typename OtherNumber, typename OtherUnit>
  friend class UnitMeasure;

  template <typename Number1, typename Number2, typename Unit2>
  friend inline constexpr auto operator*(Number1 a, UnitMeasure<Number2, Unit2> b)
      -> UnitMeasure<decltype(any<Number1>() * any<Number2>()), Unit2>;
};

template <typename Number, typename Unit>
constexpr UnitMeasure<Number, Unit> UnitMeasure<Number, Unit>::ONE;

template <typename Number1, typename Number2, typename Unit>
inline constexpr auto operator*(Number1 a, UnitMeasure<Number2, Unit> b)
    -> UnitMeasure<decltype(any<Number1>() * any<Number2>()), Unit> {
  return UnitMeasure<decltype(any<Number1>() * any<Number2>()), Unit>(a * b.value);
}

template <typename Number1, typename Number2, typename Unit, typename Unit2>
inline constexpr auto operator*(UnitRatio<Number1, Unit2, Unit> ratio,
                                UnitMeasure<Number2, Unit> measure)
    -> decltype(measure * ratio) {
  return measure * ratio;
}

}  // namespace capnproto

#endif  // CAPNPROTO_UNIT_H_
