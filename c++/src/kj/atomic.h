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

#include "common.h"

#if _MSC_VER && !defined(__clang__)
#include <atomic>
#endif

KJ_BEGIN_HEADER

namespace kj {

// Atomic operations on ordinary, suitably-aligned objects. These are intentionally a small
// wrapper around the compiler primitives rather than an atomic container: several data structures
// in KJ need to preserve their layout or initialize their storage statically.
enum class AtomicMemoryOrder {
  RELAXED,
  ACQUIRE,
  RELEASE,
  ACQUIRE_RELEASE,
  SEQUENTIAL
};

namespace _ {  // private
#if _MSC_VER && !defined(__clang__)
inline constexpr std::memory_order toStdMemoryOrder(AtomicMemoryOrder order) {
  switch (order) {
    case AtomicMemoryOrder::RELAXED: return std::memory_order_relaxed;
    case AtomicMemoryOrder::ACQUIRE: return std::memory_order_acquire;
    case AtomicMemoryOrder::RELEASE: return std::memory_order_release;
    case AtomicMemoryOrder::ACQUIRE_RELEASE: return std::memory_order_acq_rel;
    case AtomicMemoryOrder::SEQUENTIAL: return std::memory_order_seq_cst;
  }
  return std::memory_order_seq_cst;
}
#else
inline constexpr int toGnuMemoryOrder(AtomicMemoryOrder order) {
  switch (order) {
    case AtomicMemoryOrder::RELAXED: return __ATOMIC_RELAXED;
    case AtomicMemoryOrder::ACQUIRE: return __ATOMIC_ACQUIRE;
    case AtomicMemoryOrder::RELEASE: return __ATOMIC_RELEASE;
    case AtomicMemoryOrder::ACQUIRE_RELEASE: return __ATOMIC_ACQ_REL;
    case AtomicMemoryOrder::SEQUENTIAL: return __ATOMIC_SEQ_CST;
  }
  return __ATOMIC_SEQ_CST;
}
#endif
}  // namespace _ (private)

template <typename T>
inline T atomicLoad(const volatile T* ptr, AtomicMemoryOrder order) {
#if _MSC_VER && !defined(__clang__)
  return std::atomic_load_explicit(
      reinterpret_cast<const volatile std::atomic<T>*>(ptr), _::toStdMemoryOrder(order));
#else
  return __atomic_load_n(ptr, _::toGnuMemoryOrder(order));
#endif
}

template <typename T, typename U>
inline void atomicStore(volatile T* ptr, U&& value, AtomicMemoryOrder order) {
#if _MSC_VER && !defined(__clang__)
  std::atomic_store_explicit(reinterpret_cast<volatile std::atomic<T>*>(ptr),
      static_cast<T>(kj::fwd<U>(value)), _::toStdMemoryOrder(order));
#else
  __atomic_store_n(ptr, static_cast<T>(kj::fwd<U>(value)), _::toGnuMemoryOrder(order));
#endif
}

template <typename T, typename U>
inline T atomicExchange(volatile T* ptr, U&& value, AtomicMemoryOrder order) {
#if _MSC_VER && !defined(__clang__)
  return std::atomic_exchange_explicit(reinterpret_cast<volatile std::atomic<T>*>(ptr),
      static_cast<T>(kj::fwd<U>(value)), _::toStdMemoryOrder(order));
#else
  return __atomic_exchange_n(
      ptr, static_cast<T>(kj::fwd<U>(value)), _::toGnuMemoryOrder(order));
#endif
}

template <typename T, typename U>
inline bool atomicCompareExchange(volatile T* ptr, T* expected, U&& desired, bool weak,
                                  AtomicMemoryOrder successOrder,
                                  AtomicMemoryOrder failureOrder) {
#if _MSC_VER && !defined(__clang__)
  auto atomicPtr = reinterpret_cast<volatile std::atomic<T>*>(ptr);
  if (weak) {
    return std::atomic_compare_exchange_weak_explicit(atomicPtr, expected,
        static_cast<T>(kj::fwd<U>(desired)), _::toStdMemoryOrder(successOrder),
        _::toStdMemoryOrder(failureOrder));
  } else {
    return std::atomic_compare_exchange_strong_explicit(atomicPtr, expected,
        static_cast<T>(kj::fwd<U>(desired)), _::toStdMemoryOrder(successOrder),
        _::toStdMemoryOrder(failureOrder));
  }
#else
  return __atomic_compare_exchange_n(ptr, expected, static_cast<T>(kj::fwd<U>(desired)), weak,
      _::toGnuMemoryOrder(successOrder), _::toGnuMemoryOrder(failureOrder));
#endif
}

template <typename T, typename U>
inline T atomicAddFetch(volatile T* ptr, U value, AtomicMemoryOrder order) {
#if _MSC_VER && !defined(__clang__)
  return std::atomic_fetch_add_explicit(reinterpret_cast<volatile std::atomic<T>*>(ptr),
      static_cast<T>(value), _::toStdMemoryOrder(order)) + static_cast<T>(value);
#else
  return __atomic_add_fetch(ptr, static_cast<T>(value), _::toGnuMemoryOrder(order));
#endif
}

template <typename T, typename U>
inline T atomicSubFetch(volatile T* ptr, U value, AtomicMemoryOrder order) {
#if _MSC_VER && !defined(__clang__)
  return std::atomic_fetch_sub_explicit(reinterpret_cast<volatile std::atomic<T>*>(ptr),
      static_cast<T>(value), _::toStdMemoryOrder(order)) - static_cast<T>(value);
#else
  return __atomic_sub_fetch(ptr, static_cast<T>(value), _::toGnuMemoryOrder(order));
#endif
}

template <typename T, typename U>
inline T atomicFetchAnd(volatile T* ptr, U value, AtomicMemoryOrder order) {
#if _MSC_VER && !defined(__clang__)
  return std::atomic_fetch_and_explicit(reinterpret_cast<volatile std::atomic<T>*>(ptr),
      static_cast<T>(value), _::toStdMemoryOrder(order));
#else
  return __atomic_fetch_and(ptr, static_cast<T>(value), _::toGnuMemoryOrder(order));
#endif
}

inline void atomicThreadFence(AtomicMemoryOrder order) {
#if _MSC_VER && !defined(__clang__)
  std::atomic_thread_fence(_::toStdMemoryOrder(order));
#else
  __atomic_thread_fence(_::toGnuMemoryOrder(order));
#endif
}

}  // namespace kj

KJ_END_HEADER
