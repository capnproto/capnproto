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

#if __linux__ && !defined(KJ_FUTEX)
#define KJ_USE_FUTEX 1
#endif

#if !KJ_USE_FUTEX
// On Linux we use futex.  On other platforms we wrap pthreads.
// TODO(someday):  Write efficient low-level locking primitives for other platforms.
#include <pthread.h>
#endif

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

  enum Exclusivity {
    EXCLUSIVE,
    SHARED
  };

  void lock(Exclusivity exclusivity);
  void unlock(Exclusivity exclusivity);

private:
#if KJ_USE_FUTEX
  uint futex;
  // bit 31 (msb) = set if exclusive lock held
  // bit 30 (msb) = set if threads are waiting for exclusive lock
  // bits 0-29 = count of readers; If an exclusive lock is held, this is the count of threads
  //   waiting for a read lock, otherwise it is the count of threads that currently hold a read
  //   lock.

  static constexpr uint EXCLUSIVE_HELD = 1u << 31;
  static constexpr uint EXCLUSIVE_REQUESTED = 1u << 30;
  static constexpr uint SHARED_COUNT_MASK = EXCLUSIVE_REQUESTED - 1;

#else
  mutable pthread_rwlock_t mutex;
#endif
};

class Once {
  // Internal implementation details.  See `Lazy<T>`.

public:
#if KJ_USE_FUTEX
  inline Once(): futex(UNINITIALIZED) {}
#else
  Once();
  ~Once();
#endif
  KJ_DISALLOW_COPY(Once);

  class Initializer {
  public:
    virtual void run() = 0;
  };

  void runOnce(Initializer& init);

  inline bool isInitialized() noexcept {
    // Fast path check to see if runOnce() would simply return immediately.
#if KJ_USE_FUTEX
    return __atomic_load_n(&futex, __ATOMIC_ACQUIRE) == INITIALIZED;
#else
    return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE);
#endif
  }

private:
#if KJ_USE_FUTEX
  uint futex;

  static constexpr uint UNINITIALIZED = 0;
  static constexpr uint INITIALIZING = 1;
  static constexpr uint INITIALIZING_WITH_WAITERS = 2;
  static constexpr uint INITIALIZED = 3;

#else
  bool initialized;
  pthread_mutex_t mutex;
#endif
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
  inline ~Locked() {
    if (mutex != nullptr) mutex->unlock(isConst<T>() ? _::Mutex::SHARED : _::Mutex::EXCLUSIVE);
  }

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
  // will deadlock.  Recursive write locks are usually a sign of bad design.
  //
  // Unfortunately, **READ LOCKS ARE NOT RECURSIVE** either.  Common sense says they should be.
  // But on many operating systems (BSD, OSX), recursively read-locking a pthread_rwlock is
  // actually unsafe.  The problem is that writers are "prioritized" over readers, so a read lock
  // request will block if any write lock requests are outstanding.  So, if thread A takes a read
  // lock, thread B requests a write lock (and starts waiting), and then thread A tries to take
  // another read lock recursively, the result is deadlock.

public:
  template <typename... Params>
  explicit MutexGuarded(Params&&... params);
  // Initialize the mutex-guarded object by passing the given parameters to its constructor.

  Locked<T> lockExclusive() const;
  // Exclusively locks the object and returns it.  The returned `Locked<T>` can be passed by
  // move, similar to `Own<T>`.
  //
  // This method is declared `const` in accordance with KJ style rules which say that constness
  // should be used to indicate thread-safety.  It is safe to share a const pointer between threads,
  // but it is not safe to share a mutable pointer.  Since the whole point of MutexGuarded is to
  // be shared between threads, its methods should be const, even though locking it produces a
  // non-const pointer to the contained object.

  Locked<const T> lockShared() const;
  // Lock the value for shared access.  Multiple shared locks can be taken concurrently, but cannot
  // be held at the same time as a non-shared lock.

  inline const T& getWithoutLock() const { return value; }
  inline T& getWithoutLock() { return value; }
  // Escape hatch for cases where some external factor guarantees that it's safe to get the
  // value.  You should treat these like const_cast -- be highly suspicious of any use.

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
inline Locked<T> MutexGuarded<T>::lockExclusive() const {
  mutex.lock(_::Mutex::EXCLUSIVE);
  return Locked<T>(mutex, value);
}

template <typename T>
inline Locked<const T> MutexGuarded<T>::lockShared() const {
  mutex.lock(_::Mutex::SHARED);
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
