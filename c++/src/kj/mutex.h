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

#ifndef KJ_MUTEX_H_
#define KJ_MUTEX_H_

#include "memory.h"

// For now, we use pthreads.
// TODO(someday):  On Linux, use raw futexes.  pthreads are bloated with optional features and
//   debugging bookkeeping that aren't worth the cost.  A mutex should be four bytes, not forty,
//   and uncontended operations should be entirely inline!
#include <pthread.h>

namespace kj {

// =======================================================================================
// Private details -- public interfaces follow below.

namespace _ {  // private

class Mutex {
  // Internal implementation details.  See `MutexGuarded<T>`.

public:
  Mutex();
  ~Mutex();
  KJ_DISALLOW_COPY(Mutex);

  void lock() noexcept;
  void readLock() noexcept;
  void unlock(bool lockedForRead) noexcept;

private:
  mutable pthread_rwlock_t mutex;
};

class Once {
  // Internal implementation details.  See `Lazy<T>`.

public:
  Once();
  ~Once();
  KJ_DISALLOW_COPY(Once);

  class Initializer {
  public:
    virtual void run() = 0;
  };

  void runOnce(Initializer& init);

  inline bool isInitialized() noexcept {
    // Fast path check to see if runOnce() would simply return immediately.
    return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE);
  }

private:
  bool initialized;
  pthread_mutex_t mutex;
};

}  // namespace _ (private)

// =======================================================================================
// Public interface

template <typename T>
class Locked {
  // Return type for `MutexGuarded<T>::lock()`.  `Locked<T>` provides access to the guarded object
  // and unlocks the mutex when it goes out of scope.

public:
  KJ_DISALLOW_COPY(Locked);
  inline Locked(): mutex(nullptr), ptr(nullptr) {}
  inline Locked(Locked&& other): mutex(other.mutex), ptr(other.ptr) {
    other.mutex = nullptr;
    other.ptr = nullptr;
  }
  inline ~Locked() { if (mutex != nullptr) mutex->unlock(isConst<T>()); }

  inline Locked& operator=(Locked&& other) {
    if (mutex != nullptr) mutex->unlock(isConst<T>());
    mutex = other.mutex;
    ptr = other.ptr;
    other.mutex = nullptr;
    other.ptr = nullptr;
    return *this;
  }

  inline T* operator->() { return ptr; }
  inline const T* operator->() const { return ptr; }
  inline T& operator*() { return *ptr; }
  inline const T& operator*() const { return *ptr; }
  inline T* get() { return ptr; }
  inline const T* get() const { return ptr; }
  inline operator T*() { return ptr; }
  inline operator const T*() const { return ptr; }

private:
  _::Mutex* mutex;
  T* ptr;

  inline Locked(_::Mutex& mutex, T& value): mutex(&mutex), ptr(&value) {}

  template <typename U>
  friend class MutexGuarded;
};

template <typename T>
class MutexGuarded {
  // An object of type T, guarded by a mutex.  In order to access the object, you must lock it.
  //
  // Write locks are not "recursive" -- trying to lock again in a thread that already holds a lock
  // will deadlock.  If you think you need recursive locks, you are wrong.  Get over it.

public:
  template <typename... Params>
  explicit MutexGuarded(Params&&... params);
  // Initialize the mutex-guarded object by passing the given parameters to its constructor.

  Locked<T> lock() const;
  // Locks the mutex and returns the guarded object.  The returned `Locked<T>` can be passed by
  // move, similar to `Own<T>`.
  //
  // This method is declared `const` in accordance with KJ style rules which say that constness
  // should be used to indicate thread-safety.  It is safe to share a const pointer between threads,
  // but it is not safe to share a mutable pointer.  Since the whole point of MutexGuarded is to
  // be shared between threads, its methods should be const, even though locking it produces a
  // non-const pointer to the contained object.

  Locked<const T> lockForRead() const;
  // Lock the value for read-only access.  Multiple read-only locks can be taken concurrently, as
  // long as there are no writers.

private:
  mutable _::Mutex mutex;
  mutable T value;
};

template <typename T>
class MutexGuarded<const T> {
  // MutexGuarded cannot guard a const type.  This would be pointless anyway, and would complicate
  // the implementation of Locked<T>, which uses constness to decide what kind of lock it holds.
  static_assert(sizeof(T) < 0, "MutexGuarded's type cannot be const.");
};

template <typename T>
class Lazy {
  // A lazily-initialized value.

public:
  template <typename Func>
  T& get(Func&& init);
  template <typename Func>
  const T& get(Func&& init) const;
  // The first thread to call get() will invoke the given init function to construct the value.
  // Other threads will block until construction completes, then return the same value.
  //
  // `init` is a functor(typically a lambda) which takes `SpaceFor<T>&` as its parameter and returns
  // `Own<T>`.  If `init` throws an exception, the exception is propagated out of that thread's
  // call to `get()`, and subsequent calls behave as if `get()` hadn't been called at all yet --
  // in other words, subsequent calls retry initialization until it succeeds.

private:
  mutable _::Once once;
  mutable SpaceFor<T> space;
  mutable Own<T> value;

  template <typename Func>
  class InitImpl;
};

// =======================================================================================
// Inline implementation details

template <typename T>
template <typename... Params>
inline MutexGuarded<T>::MutexGuarded(Params&&... params)
    : value(kj::fwd<Params>(params)...) {}

template <typename T>
inline Locked<T> MutexGuarded<T>::lock() const {
  mutex.lock();
  return Locked<T>(mutex, value);
}

template <typename T>
inline Locked<const T> MutexGuarded<T>::lockForRead() const {
  mutex.readLock();
  return Locked<const T>(mutex, value);
}

template <typename T>
template <typename Func>
class Lazy<T>::InitImpl: public _::Once::Initializer {
public:
  inline InitImpl(const Lazy<T>& lazy, Func&& func): lazy(lazy), func(kj::fwd<Func>(func)) {}

  void run() override {
    lazy.value = func(lazy.space);
  }

private:
  const Lazy<T>& lazy;
  Func func;
};

template <typename T>
template <typename Func>
inline T& Lazy<T>::get(Func&& init) {
  if (!once.isInitialized()) {
    InitImpl<Func> initImpl(*this, kj::fwd<Func>(init));
    once.runOnce(initImpl);
  }
  return *value;
}

template <typename T>
template <typename Func>
inline const T& Lazy<T>::get(Func&& init) const {
  if (!once.isInitialized()) {
    InitImpl<Func> initImpl(*this, kj::fwd<Func>(init));
    once.runOnce(initImpl);
  }
  return *value;
}

}  // namespace kj

#endif  // KJ_MUTEX_H_
